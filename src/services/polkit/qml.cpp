#include "qml.hpp"

#include <grp.h>
#include <polkit-qt6-1/polkitqt1-details.h>
#include <polkit-qt6-1/polkitqt1-subject.h>
#include <pwd.h>
#include <qdir.h>
#include <qfile.h>
#include <qtimer.h>

namespace qs::service::polkit {

static const QString emptyString;

Identity::Identity(
    id_t id,
    QString name,
    QString displayName,
    QString icon,
    bool isGroup,
    PolkitQt1::Identity polkitIdentity,
    QObject* parent
)
    : QObject(parent)
    , polkitIdentity(std::move(polkitIdentity))
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

PolkitAgent::PolkitAgent(QObject* parent): PolkitQt1::Agent::Listener(parent) {}

PolkitAgent::~PolkitAgent() {
	// First, destroy all but the first request in the queue, so that the cancel
	// method doesn't start a new one.
	for (; queuedRequests.size() > 1; queuedRequests.pop_back()) {
		AuthRequest& req = queuedRequests.back();
		qDebug() << "PolkitAgent: destroying queued authentication request for action" << req.actionId;
		if (req.result) {
			req.result->setError("PolkitAgent is being destroyed");
			req.result->setCompleted();
		}
	}

	if (!queuedRequests.empty()) {
		cancelAuthenticationRequest();
	}

	if (auto it = registeredAgentsByPath.find(mPath);
	    it != registeredAgentsByPath.end() && it->second == this)
	{
		registeredAgentsByPath.erase(mPath);

		// In case of config reloads new objects are constructed before the old ones
		// are destroyed. Therefore the new agent cannot take over the path still in
		// use by the old agent.
		if (auto it = waitingAgentsByPath.find(mPath); it != waitingAgentsByPath.end()) {
			// Retry registration for the waiting agent on the next tick after the
			// destructor has finished and deregistered this agent.
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
		qDebug() << "PolkitAgent: registering listener on path" << mPath;
		PolkitQt1::UnixSessionSubject session(getpid());
		if (registerListener(session, mPath)) {
			registeredAgentsByPath[mPath] = this;
			// If we were previously waiting to acquire this path, we no longer
			// are.
			if (auto it = waitingAgentsByPath.find(mPath);
			    it != waitingAgentsByPath.end() && it->second == this)
			{
				waitingAgentsByPath.erase(it);
			}
		} else {
			qWarning() << "PolkitAgent: failed to register listener on path" << mPath;
			// We may be able to register later if the current holder of the path
			// goes away.
			waitingAgentsByPath[mPath] = this;
		}
	} else {
		qWarning() << "PolkitAgent: no path set, not registering listener.";
	}
}

void PolkitAgent::submit(const QString& value) {
	qDebug() << "PolkitAgent: submitting response for authentication request";
	if (currentSession) {
		currentSession->setResponse(value);
	}
}

void PolkitAgent::cancelAuthenticationRequest() {
	qDebug() << "PolkitAgent: cancelling authentication request by user request.";

	if (currentSession) {
		currentSession->cancel();
		currentSession->result()->setCompleted();
		isCancelled = true;
	}
}

QString PolkitAgent::path() const { return mPath; }

void PolkitAgent::setPath(const QString& path) {
	if (mPath.isEmpty()) {
		mPath = path;
	} else if (mPath != path) {
		qWarning() << "PolkitAgent: cannot change path after it has been set.";
	}
}

bool PolkitAgent::isActive() const { return !queuedRequests.empty(); }

const QString& PolkitAgent::activeMessage() const {
	if (queuedRequests.empty()) {
		return emptyString;
	}
	return queuedRequests.front().message;
}

const QString& PolkitAgent::activeIconName() const {
	if (queuedRequests.empty()) {
		return emptyString;
	}
	return queuedRequests.front().iconName;
}

const QString& PolkitAgent::activeActionId() const {
	if (queuedRequests.empty()) {
		return emptyString;
	}
	return queuedRequests.front().actionId;
}

ObjectModel<Identity>* PolkitAgent::activeIdentities() { return &mIdentities; }

Identity* PolkitAgent::selectedIdentity() const { return mSelectedIdentity; }

void PolkitAgent::setSelectedIdentity(Identity* identity) {
	if (queuedRequests.empty()) {
		return;
	}

	if (mSelectedIdentity == identity) {
		return;
	}

	qDebug() << "PolkitAgent: changing selected identity to"
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

void PolkitAgent::initiateAuthentication(
    const QString& actionId,
    const QString& message,
    const QString& iconName,
    const PolkitQt1::Details&,
    const QString& cookie,
    const PolkitQt1::Identity::List& identities,
    PolkitQt1::Agent::AsyncResult* result
) {
	qDebug() << "PolkitAgent: incoming authentication request for action" << actionId;

	queuedRequests.emplace_back(actionId, message, iconName, cookie, identities, result);

	if (queuedRequests.size() == 1) {
		activateAuthenticationRequest();
	}
}

bool PolkitAgent::initiateAuthenticationFinish() { return true; }

void PolkitAgent::cancelAuthentication() {
	qDebug() << "PolkitAgent: cancelling authentication request from agent";

	if (queuedRequests.empty()) {
		return;
	}

	// Any of the queued requests may be cancelled, but polkit-qt-1 doesn't tell
	// us which one. We assume it's the current one, but this might be wrong.
	if (queuedRequests.size() > 1) {
		qWarning() << "PolkitAgent: cancelling an authentication request while others are queued. This "
		              "may lead to errors.";
	}

	if (currentSession) {
		currentSession->cancel();
		currentSession->result()->setCompleted();
	}
	isCancelled = true;

	emit authenticationRequestCancelled();
}

void PolkitAgent::request(const QString& message, bool echo) {
	qDebug() << "PolkitAgent: requesting user input for authentication";

	if (mInputRequest) {
		mInputRequest->deleteLater();
	}

	mInputRequest = new InputRequest(message, echo, currentSession);
	emit inputRequestChanged();
}

void PolkitAgent::completed(bool gainedAuthorization) {
	qDebug() << "PolkitAgent: authentication request completed";

	if (gainedAuthorization) {
		currentSession->result()->setCompleted();

		emit authenticationSucceeded();

		finishAuthenticationRequest();
	} else if (isCancelled) {
		finishAuthenticationRequest();
	} else {
		emit authenticationFailed();

		setupSession();
	}
}

void PolkitAgent::showError(const QString& message) {
	qDebug() << "PolkitAgent: showing error message:" << message;

	if (mSubMessage) {
		mSubMessage->deleteLater();
	}

	mSubMessage = new SubMessage(message, true, currentSession);
	emit subMessageChanged();
}

void PolkitAgent::showInfo(const QString& message) {
	qDebug() << "PolkitAgent: showing info message:" << message;

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

	AuthRequest& req = queuedRequests.front();

	qDebug() << "PolkitAgent: activating authentication request for action" << req.actionId;

	// TODO: Consider a better clearing strategy. This seems to be the only concise
	//       way currently exposed. Also, consider if we're leaking the identity
	//       objects.
	mIdentities.diffUpdate({});

	for (auto& identity: req.identities) {
		Identity* obj;
		// PolkitQt1::Identity doesn't expose a cleaner way to determine the
		// kind of identity, unfortunately.
		// By dropping down into the GObject interface to Polkit, we could
		// use POLKIT_IDENTITY_IS_UNIX_USER.
		auto identityString = identity.toString();
		if (identityString.startsWith("unix-user:")) {
			auto uid = identity.toUnixUserIdentity().uid();
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
			    identity,
			    &mIdentities
			);
		}

		if (identityString.startsWith("unix-group:")) {
			auto gid = identity.toUnixGroupIdentity().gid();
			auto gr = getgrgid(gid);
			auto name = (gr && gr->gr_name && *gr->gr_name) ? QString::fromUtf8(gr->gr_name)
			                                                : QString::number(gid);
			obj = new Identity(
			    gid,
			    name,
			    name,
			    QString(), // no icon for groups
			    true,
			    identity,
			    &mIdentities
			);
		}

		// For now, other identity types (currently only PolkitUnixNetgroup)
		// are not supported.

		if (obj) {
			mIdentities.insertObject(obj);
		}
	}

	mSelectedIdentity = mIdentities.valueList().isEmpty() ? nullptr : mIdentities.valueList().first();
	if (mSelectedIdentity == nullptr) {
		qWarning(
		) << "PolkitAgent: no supported identities available for authentication request, cancelling.";

		req.result->setError("Error requesting authentication: no supported identities available.");
		req.result->setCompleted();

		queuedRequests.pop_front();
		return;
	}

	setupSession();

	emit authenticationRequestStarted();
	emit isActiveChanged();
}

void PolkitAgent::setupSession() {
	AuthRequest& req = queuedRequests.front();

	qDebug() << "PolkitAgent: setting up authentication session for identity"
	         << (mSelectedIdentity ? mSelectedIdentity->name() : "<null>");

	currentSession =
	    new PolkitQt1::Agent::Session(mSelectedIdentity->polkitIdentity, req.cookie, req.result);

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

	qDebug() << "PolkitAgent: finishing authentication request for action"
	         << queuedRequests.front().actionId;

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
