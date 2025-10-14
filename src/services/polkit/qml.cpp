#include "qml.hpp"
#include "listener.hpp"
#include "session.hpp"

#include <grp.h>
#include <pwd.h>
#include <qdir.h>
#include <qfile.h>
#include <qtimer.h>
#include <polkitagent/polkitagent.h>
#include <polkit/polkit.h>

#include "../../core/logcat.hpp"

namespace {
QS_LOGGING_CATEGORY(logPolkit, "quickshell.service.polkit");
}

namespace qs::service::polkit {

static const QString emptyString;

Identity::Identity(
    id_t id,
    QString name,
    QString displayName,
    QString icon,
    bool isGroup,
    PolkitIdentity* polkitIdentity,
    QObject* parent
)
    : QObject(parent)
    , polkitIdentity(polkitIdentity)
    , mId(id)
    , mName(std::move(name))
    , mDisplayName(std::move(displayName))
    , mIcon(std::move(icon))
    , mIsGroup(isGroup) {}

Identity::~Identity() = default;

id_t Identity::id() const { return mId; }
const QString& Identity::name() const { return mName; }
const QString& Identity::displayName() const { return mDisplayName; }
const QString& Identity::icon() const { return mIcon; }
bool Identity::isGroup() const { return mIsGroup; }

SubMessage::SubMessage(QString text, bool isError, QObject* parent)
    : QObject(parent)
    , mText(std::move(text))
    , mIsError(isError) {}

SubMessage::~SubMessage() = default;

const QString& SubMessage::text() const { return mText; }
bool SubMessage::isError() const { return mIsError; }

InputRequest::InputRequest(QString message, bool echo, QObject* parent)
    : QObject(parent)
    , mMessage(std::move(message))
    , mEcho(echo) {}

InputRequest::~InputRequest() = default;

const QString& InputRequest::message() const { return mMessage; }
bool InputRequest::echo() const { return mEcho; }

static std::unordered_map<QString, PolkitAgent*> registeredAgentsByPath {};
static std::unordered_map<QString, PolkitAgent*> waitingAgentsByPath {};

PolkitAgent::PolkitAgent(QObject* parent): QObject(parent), listener(qs_polkit_agent_new(this)) {}

PolkitAgent::~PolkitAgent() {
	// First, destroy all but the first request in the queue, so that the cancel
	// method doesn't start a new one.
	for (; queuedRequests.size() > 1; queuedRequests.pop_back()) {
		AuthRequest& req = *queuedRequests.back();
		qCDebug(logPolkit) << "destroying queued authentication request for action" << req.actionId;
		req.cancel("PolkitAgent is being destroyed");
	}

	if (!queuedRequests.empty()) {
		cancelAuthenticationRequest();
	}

	qs_polkit_agent_unregister(listener);
	g_object_unref(listener);

	if (auto it = registeredAgentsByPath.find(mPath);
	    it != registeredAgentsByPath.end() && it->second == this)
	{
		registeredAgentsByPath.erase(mPath);

		// In case of config reloads new objects are constructed before the old ones
		// are destroyed. Therefore the new agent cannot take over the path still in
		// use by the old agent.
		if (auto it = waitingAgentsByPath.find(mPath); it != waitingAgentsByPath.end()) {
			// Retry registration for the waiting agent on the next tick.
			QTimer::singleShot(0, it->second, &PolkitAgent::componentComplete);
		}
	}

	if (auto it = waitingAgentsByPath.find(mPath);
	    it != waitingAgentsByPath.end() && it->second == this)
	{
		waitingAgentsByPath.erase(it);
	}
}

void PolkitAgent::classBegin() {
	// Nothing to do here.
}

void PolkitAgent::componentComplete() {
	if (!mPath.isEmpty()) {
		qCDebug(logPolkit) << "registering listener on path" << mPath;
		if (qs_polkit_agent_register(listener)) {
			registeredAgentsByPath[mPath] = this;
			// If we were previously waiting to acquire this path, we no longer
			// are.
			if (auto it = waitingAgentsByPath.find(mPath);
			    it != waitingAgentsByPath.end() && it->second == this)
			{
				waitingAgentsByPath.erase(it);
			}
		} else {
			qCWarning(logPolkit) << "failed to register listener on path" << mPath;
			// We may be able to register later if the current holder of the path
			// goes away.
			waitingAgentsByPath[mPath] = this;
		}
	} else {
		qCWarning(logPolkit) << "no path set, not registering listener.";
	}
}

void PolkitAgent::submit(const QString& value) {
	qCDebug(logPolkit) << "submitting response for authentication request";
	if (currentSession) {
		currentSession->respond(value);
	}

	// The input request is handled by the above submission.
	mInputRequest->deleteLater();
	mInputRequest = nullptr;
	emit inputRequestChanged();
}

void PolkitAgent::cancelAuthenticationRequest() {
	qCDebug(logPolkit) << "cancelling authentication request by user request.";

	if (currentSession) {
		currentSession->cancel();
		isCancelled = true;
	}
}

QString PolkitAgent::path() const { return mPath; }

void PolkitAgent::setPath(const QString& path) {
	if (mPath.isEmpty()) {
		mPath = path;
	} else if (mPath != path) {
		qCWarning(logPolkit) << "cannot change path after it has been set.";
	}
}

bool PolkitAgent::isActive() const { return !queuedRequests.empty(); }

const QString& PolkitAgent::activeMessage() const {
	if (queuedRequests.empty()) {
		return emptyString;
	}
	return queuedRequests.front()->message;
}

const QString& PolkitAgent::activeIconName() const {
	if (queuedRequests.empty()) {
		return emptyString;
	}
	return queuedRequests.front()->iconName;
}

const QString& PolkitAgent::activeActionId() const {
	if (queuedRequests.empty()) {
		return emptyString;
	}
	return queuedRequests.front()->actionId;
}

const QList<Identity*>& PolkitAgent::activeIdentities() const { return mIdentities; }

Identity* PolkitAgent::selectedIdentity() const { return mSelectedIdentity; }

void PolkitAgent::setSelectedIdentity(Identity* identity) {
	if (queuedRequests.empty()) {
		return;
	}

	if (mSelectedIdentity == identity) {
		return;
	}

	qCDebug(logPolkit) << "changing selected identity to"
	         << (identity ? identity->name() : "<null>");

	mSelectedIdentity = identity;
	emit selectedIdentityChanged();

	if (currentSession) {
		currentSession->deleteLater();
	}
	setupSession();
}

InputRequest* PolkitAgent::inputRequest() const { return mInputRequest; }

SubMessage* PolkitAgent::subMessage() const { return mSubMessage; }

void PolkitAgent::initiateAuthentication(AuthRequest* request) {
	qCDebug(logPolkit) << "incoming authentication request for action" << request->actionId;

	queuedRequests.emplace_back(request);

	if (queuedRequests.size() == 1) {
		activateAuthenticationRequest();
	}
}

void PolkitAgent::cancelAuthentication(AuthRequest* request) {
	qCDebug(logPolkit) << "cancelling authentication request from agent";

	if (!queuedRequests.empty() && request == queuedRequests.front()) {
		if (currentSession) {
			currentSession->cancel();
		}
		isCancelled = true;

		emit authenticationRequestCancelled();
	} else if (auto it = std::find(queuedRequests.begin(), queuedRequests.end(), request);
	           it != queuedRequests.end())
	{
		qCDebug(logPolkit) << "removing queued authentication request for action"
		         << (*it)->actionId;
		(*it)->cancel("Authentication request was cancelled");
		(*it)->deleteLater();
		queuedRequests.erase(it);
	} else {
		qCWarning(logPolkit) << "the cancelled request was not found in the queue.";
	}
}

void PolkitAgent::request(const QString& message, bool echo) {
	qCDebug(logPolkit) << "requesting user input for authentication";

	if (mInputRequest) {
		mInputRequest->deleteLater();
	}

	mInputRequest = new InputRequest(message, echo, currentSession);
	emit inputRequestChanged();
}

void PolkitAgent::completed(bool gainedAuthorization) {
	qCDebug(logPolkit) << "authentication request completed";

	auto& req = *queuedRequests.front();
	if (gainedAuthorization) {
		emit authenticationSucceeded();

		req.complete();
		finishAuthenticationRequest();
	} else if (isCancelled) {
		req.cancel("Authentication request was cancelled");
		finishAuthenticationRequest();
	} else {
		emit authenticationFailed();

		setupSession();
	}
}

void PolkitAgent::showError(const QString& message) {
	qCDebug(logPolkit) << "showing error message:" << message;

	if (mSubMessage) {
		mSubMessage->deleteLater();
	}

	mSubMessage = new SubMessage(message, true, currentSession);
	emit subMessageChanged();
}

void PolkitAgent::showInfo(const QString& message) {
	qCDebug(logPolkit) << "showing info message:" << message;

	if (mSubMessage) {
		mSubMessage->deleteLater();
	}

	mSubMessage = new SubMessage(message, false, currentSession);
	emit subMessageChanged();
}

void PolkitAgent::activateAuthenticationRequest() {
	if (queuedRequests.empty()) {
		return;
	}

	AuthRequest& req = *queuedRequests.front();

	qCDebug(logPolkit) << "activating authentication request for action" << req.actionId;

	for (auto identity: mIdentities) {
		delete identity;
	}
	mIdentities.clear();

	for (auto identity: req.identities) {
		Identity* obj;

		if (POLKIT_IS_UNIX_USER(identity)) {
			auto uid = polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity));
			auto pw = getpwuid(uid);
			auto name = (pw && pw->pw_name && *pw->pw_name) ? QString::fromUtf8(pw->pw_name)
			                                                : QString::number(uid);
			QString icon;
			if (pw && pw->pw_dir && *pw->pw_dir) {
				icon = QString::fromUtf8(pw->pw_dir) + QDir::separator() + ".face.icon";
				if (!QFile::exists(icon)) {
					icon.clear();
				}
			}
			obj = new Identity(
			    uid,
			    name,
			    (pw && pw->pw_gecos && *pw->pw_gecos) ? QString::fromUtf8(pw->pw_gecos) : name,
			    icon,
			    false,
			    identity
			);
		}

		if (POLKIT_IS_UNIX_GROUP(identity)) {
			auto gid = polkit_unix_group_get_gid(POLKIT_UNIX_GROUP(identity));
			auto gr = getgrgid(gid);
			auto name = (gr && gr->gr_name && *gr->gr_name) ? QString::fromUtf8(gr->gr_name)
			                                                : QString::number(gid);
			obj = new Identity(
			    gid,
			    name,
			    name,
			    QString(), // no icon for groups
			    true,
			    identity
			);
		}

		// For now, other identity types (currently only PolkitUnixNetgroup)
		// are not supported.

		if (obj) {
			mIdentities.append(obj);
		}
	}

	mSelectedIdentity = mIdentities.isEmpty() ? nullptr : mIdentities.first();
	if (mSelectedIdentity == nullptr) {
		qCWarning(logPolkit) << "no supported identities available for authentication request, cancelling.";

		req.cancel("Error requesting authentication: no supported identities available.");

		req.deleteLater();
		queuedRequests.pop_front();
		return;
	}

	setupSession();

	emit authenticationRequestStarted();
	emit isActiveChanged();
}

void PolkitAgent::setupSession() {
	AuthRequest& req = *queuedRequests.front();

	qCDebug(logPolkit) << "setting up authentication session for identity"
	         << (mSelectedIdentity ? mSelectedIdentity->name() : "<null>");

	currentSession = new Session(mSelectedIdentity->polkitIdentity, req.cookie);

	connect(currentSession, SIGNAL(request(QString, bool)), this, SLOT(request(QString, bool)));
	connect(currentSession, SIGNAL(completed(bool)), this, SLOT(completed(bool)));
	connect(currentSession, SIGNAL(showError(QString)), this, SLOT(showError(QString)));
	connect(currentSession, SIGNAL(showInfo(QString)), this, SLOT(showInfo(QString)));

	currentSession->initiate();
}

void PolkitAgent::finishAuthenticationRequest() {
	if (queuedRequests.empty()) {
		return;
	}

	qCDebug(logPolkit) << "finishing authentication request for action"
	                   << queuedRequests.front()->actionId;

	queuedRequests.front()->deleteLater();
	queuedRequests.pop_front();

	currentSession->deleteLater();
	currentSession = nullptr;

	mSelectedIdentity = nullptr;
	// These are owned by currentSession, so will be deleted with it.
	mInputRequest = nullptr;
	mSubMessage = nullptr;

	isCancelled = false;

	if (!queuedRequests.empty()) {
		activateAuthenticationRequest();
	} else {
		emit isActiveChanged();
	}
}

} // namespace qs::service::polkit
