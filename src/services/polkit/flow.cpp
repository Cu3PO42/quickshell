#include "flow.hpp"

#include "../../core/logcat.hpp"
#include "identity.hpp"
#include "qml.hpp"
#include "session.hpp"

namespace {
QS_LOGGING_CATEGORY(logPolkitState, "quickshell.service.polkit.state");
}

namespace qs::service::polkit {
AuthFlow::AuthFlow(AuthRequest* request, QList<Identity*>&& identities, QObject* parent)
    : QObject(parent)
    , mRequest(request)
    , mIdentities(std::move(identities)) {
	mSelectedIdentity = mIdentities.isEmpty() ? nullptr : mIdentities.first();
	if (!mSelectedIdentity) {
		qCCritical(logPolkitState) << "AuthFlow created with no valid identities!";
	}

	setupSession();
}

AuthFlow::~AuthFlow() { delete mRequest; };

const QString& AuthFlow::message() const { return mRequest->message; }
const QString& AuthFlow::iconName() const { return mRequest->iconName; }
const QString& AuthFlow::actionId() const { return mRequest->actionId; }
const QString& AuthFlow::cookie() const { return mRequest->cookie; }
const QList<Identity*>& AuthFlow::identities() const { return mIdentities; }

Identity* AuthFlow::selectedIdentity() const { return mSelectedIdentity; }

void AuthFlow::setSelectedIdentity(Identity* identity) {
	if (mSelectedIdentity == identity) return;
	if (!identity) return; // ignore null changes
	for (auto* id: mIdentities) {
		if (id == identity) {
			mSelectedIdentity = id;
			emit selectedIdentityChanged();
			return;
		}
	}
}

bool AuthFlow::isResponseRequired() const { return mIsResponseRequired; }
const QString& AuthFlow::inputPrompt() const { return mResponseMessage; }
bool AuthFlow::responseVisible() const { return mResponseVisible; }
const QString& AuthFlow::supplementaryMessage() const { return mExtraMessage; }
bool AuthFlow::supplementaryIsError() const { return mExtraIsError; }
bool AuthFlow::isCompleted() const { return mIsCompleted; }
bool AuthFlow::isSuccessful() const { return mIsSuccessful; }
AuthRequest* AuthFlow::authRequest() const { return mRequest; }

void AuthFlow::cancelFromAgent() {
	if (!currentSession) return;

	qCDebug(logPolkitState) << "cancelling authentication request from agent";

	mIsCancelled = true;
	currentSession->cancel();

	emit authenticationRequestCancelled();

	mRequest->cancel("Authentication request cancelled by agent.");
}

void AuthFlow::submit(const QString& value) {
	if (!currentSession) return;

	qCDebug(logPolkitState) << "submitting response to authentication request";

	currentSession->respond(value);

	mIsResponseRequired = false;
	mResponseMessage.clear();
	mResponseVisible = false;
	emit responseRequestChanged();
}

void AuthFlow::cancelAuthenticationRequest() {
	if (!currentSession) return;

	qCDebug(logPolkitState) << "cancelling authentication request by user request";

	mIsCancelled = true;
	currentSession->cancel();

	mRequest->cancel("Authentication request cancelled by user.");
}

void AuthFlow::setupSession() {
	if (currentSession) delete currentSession;

	qCDebug(logPolkitState) << "setting up session for identity" << mSelectedIdentity->name();

	currentSession = new Session(mSelectedIdentity->polkitIdentity, mRequest->cookie, this);
	QObject::connect(currentSession, &Session::request, this, &AuthFlow::request);
	QObject::connect(currentSession, &Session::completed, this, &AuthFlow::completed);
	QObject::connect(currentSession, &Session::showError, this, &AuthFlow::showError);
	QObject::connect(currentSession, &Session::showInfo, this, &AuthFlow::showInfo);
	currentSession->initiate();
}

void AuthFlow::clearState() {
	mIsResponseRequired = false;
	mResponseMessage.clear();
	mResponseVisible = false;
	mExtraMessage.clear();
	mExtraIsError = false;

	emit responseRequestChanged();
	emit supplementaryChanged();
}

void AuthFlow::request(const QString& message, bool echo) {
	mIsResponseRequired = true;
	mResponseMessage = message;
	mResponseVisible = echo;
	emit responseRequestChanged();
}

void AuthFlow::completed(bool gainedAuthorization) {
	qCDebug(logPolkitState) << "authentication session completed, gainedAuthorization ="
	                        << gainedAuthorization << ", isCancelled =" << mIsCancelled;

	if (gainedAuthorization) {
		mIsCompleted = true;
		mIsSuccessful = true;
		mRequest->complete();

		emit completedChanged();
		emit authenticationSucceeded();
	} else if (mIsCancelled) {
		mIsCompleted = true;
		mIsSuccessful = false;

		emit completedChanged();
	} else {
		emit authenticationFailed();

		clearState();
		setupSession();
	}
}

void AuthFlow::showError(const QString& message) {
	mExtraMessage = message;
	mExtraIsError = true;
	emit supplementaryChanged();
}

void AuthFlow::showInfo(const QString& message) {
	mExtraMessage = message;
	mExtraIsError = false;
	emit supplementaryChanged();
}
} // namespace qs::service::polkit
