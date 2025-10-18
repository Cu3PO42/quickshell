#include "listener.hpp"

#include <qtimer.h>

#include "qml.hpp"
#include "../../core/logcat.hpp"

namespace {
QS_LOGGING_CATEGORY(logPolkitListener, "quickshell.service.polkit.listener", QtWarningMsg);
}

typedef struct _QsPolkitAgent {
	PolkitAgentListener parent_instance;

	qs::service::polkit::PolkitAgent *agent;
	gpointer registration_handle;
} QsPolkitAgent;

G_DEFINE_TYPE(QsPolkitAgent, qs_polkit_agent, POLKIT_AGENT_TYPE_LISTENER)

static void initiate_authentication(
	PolkitAgentListener *listener,
	const gchar *actionId,
	const gchar *message,
	const gchar *iconName,
	PolkitDetails *details,
	const gchar *cookie,
	GList *identities,
	GCancellable *cancellable,
	GAsyncReadyCallback callback,
	gpointer userData
);

static gboolean initiate_authentication_finish(PolkitAgentListener *listener, GAsyncResult *result, GError **error);

static void qs_polkit_agent_init(QsPolkitAgent *self) {
	self->agent = nullptr;
	self->registration_handle = nullptr;
}

static void qs_polkit_agent_finalize(GObject *object) {
	if (G_OBJECT_CLASS(qs_polkit_agent_parent_class))
		G_OBJECT_CLASS(qs_polkit_agent_parent_class)->finalize(object);
}

static void qs_polkit_agent_class_init(QsPolkitAgentClass *klass) {
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->finalize = qs_polkit_agent_finalize;

	PolkitAgentListenerClass *listener_class = POLKIT_AGENT_LISTENER_CLASS(klass);
	listener_class->initiate_authentication = initiate_authentication;
	listener_class->initiate_authentication_finish = initiate_authentication_finish;
}

QsPolkitAgent* qs_polkit_agent_new(qs::service::polkit::PolkitAgent* parent) {
	QsPolkitAgent* self = QS_POLKIT_AGENT(g_object_new(QS_TYPE_POLKIT_AGENT, nullptr));
	self->agent = parent;
	return self;
}

static void qs_polkit_agent_register_cb(GObject*, GAsyncResult* res, gpointer userData);
void qs_polkit_agent_register(QsPolkitAgent* agent) {
	if (agent->agent->path().isEmpty()) {
		qCWarning(logPolkitListener) << "cannot register listener without a path set.";
		agent->agent->registerComplete(false);
		return;
	}

	polkit_unix_session_new_for_process(getpid(), nullptr, &qs_polkit_agent_register_cb, agent);
}

static void qs_polkit_agent_register_cb(GObject*, GAsyncResult* res, gpointer userData) {
	auto agent = static_cast<QsPolkitAgent*>(userData);

	GError* error = nullptr;
	auto subject = polkit_unix_session_new_for_process_finish(res, &error);

	if (subject == nullptr || error != nullptr) {
		qCWarning(logPolkitListener) << "failed to create subject for listener:" << (error ? error->message : "<unknown error>");
		g_clear_error(&error);
		agent->agent->registerComplete(false);
		return;
	}

	auto utf8Path = agent->agent->path().toUtf8();
	agent->registration_handle = polkit_agent_listener_register(
		POLKIT_AGENT_LISTENER(agent),
		POLKIT_AGENT_REGISTER_FLAGS_NONE,
		subject,
		utf8Path.constData(),
		nullptr,
		&error
	);

	g_object_unref(subject);

	if (error != nullptr) {
		qCWarning(logPolkitListener) << "failed to register listener:" << error->message;
		g_clear_error(&error);
		agent->agent->registerComplete(false);
		return;
	}

	agent->agent->registerComplete(true);
}

void qs_polkit_agent_unregister(QsPolkitAgent* agent) {
	if (agent->registration_handle != nullptr) {
		polkit_agent_listener_unregister(agent->registration_handle);
		agent->registration_handle = nullptr;
	}
}

static void authentication_cancelled_cb(GCancellable*, gpointer userData) {
	auto request = static_cast<qs::service::polkit::AuthRequest*>(userData);
	request->agent->cancelAuthentication(request);
}

static void initiate_authentication(
	PolkitAgentListener *listener,
	const gchar *actionId,
	const gchar *message,
	const gchar *iconName,
	PolkitDetails *,
	const gchar *cookie,
	GList *identities,
	GCancellable *cancellable,
	GAsyncReadyCallback callback,
	gpointer userData
) {
	auto self = QS_POLKIT_AGENT(listener);

	auto asyncResult = g_task_new(
		reinterpret_cast<GObject*>(self),
		nullptr,
		callback,
		userData
	);

	// Identities may be duplicated, so we use the hash to filter them out.
	std::unordered_set<guint> identitySet;
	std::vector<PolkitIdentity*> identityVector;
	for (auto item = g_list_first(identities); item != nullptr; item = g_list_next(item)) {
		auto identity = static_cast<PolkitIdentity*>(item->data);
		if (identitySet.contains(polkit_identity_hash(identity))) continue;

		identitySet.insert(polkit_identity_hash(identity));
		identityVector.push_back(identity);
		g_object_ref(identity);
	}

	auto request = new qs::service::polkit::AuthRequest {
		.actionId = QString::fromUtf8(actionId),
		.message = QString::fromUtf8(message),
		.iconName = QString::fromUtf8(iconName),
		.cookie = QString::fromUtf8(cookie),
		.identities = std::move(identityVector),

		.task = asyncResult,
		.cancellable = cancellable,
		.handlerId = 0,
		.agent = self->agent
	};

	if (cancellable != nullptr) {
		request->handlerId = g_cancellable_connect(
			cancellable,
			GCallback(authentication_cancelled_cb),
			request,
			nullptr
		);
	}

	self->agent->initiateAuthentication(request);
}

static gboolean initiate_authentication_finish(PolkitAgentListener *, GAsyncResult *result, GError **error) {
	return g_task_propagate_boolean(G_TASK(result), error);
}

namespace qs::service::polkit {
	AuthRequest::~AuthRequest() {
		for (auto identity: identities) {
			g_object_unref(identity);
		}
	}

	void AuthRequest::complete() {
		g_task_return_boolean(task, true);
	}

	void AuthRequest::cancel(const QString& reason) {
		auto utf8Reason = reason.toUtf8();
		g_task_return_new_error(task, POLKIT_ERROR, POLKIT_ERROR_CANCELLED, "%s", utf8Reason.constData());
	}

	void AuthRequest::deleteLater() {
		QTimer::singleShot(0, [this]() { delete this; });
	}
}

void qs_polkit_agent_set_parent(QsPolkitAgent* agent, qs::service::polkit::PolkitAgent* parent) {
	agent->agent = parent;
}
