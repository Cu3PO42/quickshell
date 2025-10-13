#include "session.hpp"

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
// This causes a problem with variables of the name.
#undef signals
#include <polkitagent/polkitagent.h>
#define signals Q_SIGNALS

namespace qs::service::polkit {

static void completed_cb(PolkitAgentSession*, gboolean gainedAuthorization, gpointer userData) {
	auto self = static_cast<Session*>(userData);
	emit self->completed(gainedAuthorization);

	self->destroy();
}

static void request_cb(PolkitAgentSession*, const char* message, gboolean echo, gpointer userData) {
	auto self = static_cast<Session*>(userData);
	emit self->request(QString::fromUtf8(message), echo);
}

static void show_error_cb(PolkitAgentSession*, const char* message, gpointer userData) {
	auto self = static_cast<Session*>(userData);
	emit self->showError(QString::fromUtf8(message));
}

static void show_info_cb(PolkitAgentSession*, const char* message, gpointer userData) {
	auto self = static_cast<Session*>(userData);
	emit self->showInfo(QString::fromUtf8(message));
}

Session::Session(PolkitIdentity* identity, const QString& cookie, QObject* parent)
	: QObject(parent) {
	session = polkit_agent_session_new(identity, cookie.toUtf8().constData());

	g_signal_connect(G_OBJECT(session), "completed", G_CALLBACK(completed_cb), this);
	g_signal_connect(G_OBJECT(session), "request", G_CALLBACK(request_cb), this);
	g_signal_connect(G_OBJECT(session), "show-error", G_CALLBACK(show_error_cb), this);
	g_signal_connect(G_OBJECT(session), "show-info", G_CALLBACK(show_info_cb), this);
}

Session::~Session() {
	destroy();
}

void Session::initiate() {
	polkit_agent_session_initiate(session);
}

void Session::cancel() {
	polkit_agent_session_cancel(session);
}

void Session::respond(const QString& response) {
	polkit_agent_session_response(session, response.toUtf8().constData());
}

void Session::destroy() {
	// Signals do not need to be disconnected explicitly. This happens during
	// destruction of the gobject. Since we own the session object, we can be
	// sure it is being destroyed after the unref.
	if (session) {
		g_object_unref(session);
		session = nullptr;
	}
}

} // namespace qs::service::polkit
