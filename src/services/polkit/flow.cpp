#include "flow.hpp"
#include <utility>

#include <qlist.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qqmlinfo.h>
#include <qtmetamacros.h>

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
    , mIdentities(std::move(identities))
    , mSelectedIdentity(this->mIdentities.isEmpty() ? nullptr : this->mIdentities.first()) {
	// We reject auth requests with no identities before a flow is created.
	// This should never happen.
	if (!this->mSelectedIdentity)
		qCFatal(logPolkitState) << "AuthFlow created with no valid identities!";

	for (auto* identity: this->mIdentities) {
		identity->setParent(this);
	}

	this->setupSession();
}

AuthFlow::~AuthFlow() { delete this->mRequest; };

const QString& AuthFlow::message() const { return this->mRequest->message; }
const QString& AuthFlow::iconName() const { return this->mRequest->iconName; }
const QString& AuthFlow::actionId() const { return this->mRequest->actionId; }
const QString& AuthFlow::cookie() const { return this->mRequest->cookie; }
const QList<Identity*>& AuthFlow::identities() const { return this->mIdentities; }

Identity* AuthFlow::selectedIdentity() const { return this->mSelectedIdentity; }

void AuthFlow::setSelectedIdentity(Identity* identity) {
	if (this->mSelectedIdentity == identity) return;
	if (!identity) {
		qmlWarning(this) << "Cannot set selected identity to null.";
		return;
	}
	for (auto* id: this->mIdentities) {
		if (id == identity) {
			this->mSelectedIdentity = id;
			emit this->selectedIdentityChanged();
			return;
		}
	}
}

bool AuthFlow::isResponseRequired() const { return this->mIsResponseRequired; }
const QString& AuthFlow::inputPrompt() const { return this->mResponseMessage; }
bool AuthFlow::responseVisible() const { return this->mResponseVisible; }
const QString& AuthFlow::supplementaryMessage() const { return this->mExtraMessage; }
bool AuthFlow::supplementaryIsError() const { return this->mExtraIsError; }
bool AuthFlow::isCompleted() const { return this->mIsCompleted; }
bool AuthFlow::isSuccessful() const { return this->mIsSuccessful; }
AuthRequest* AuthFlow::authRequest() const { return this->mRequest; }

void AuthFlow::cancelFromAgent() {
	if (!this->currentSession) return;

	qCDebug(logPolkitState) << "cancelling authentication request from agent";

	this->mIsCancelled = true;
	this->currentSession->cancel();

	emit this->authenticationRequestCancelled();

	this->mRequest->cancel("Authentication request cancelled by agent.");
}

void AuthFlow::submit(const QString& value) {
	if (!this->currentSession) return;

	qCDebug(logPolkitState) << "submitting response to authentication request";

	this->currentSession->respond(value);

	this->mIsResponseRequired = false;
	this->mResponseMessage.clear();
	this->mResponseVisible = false;
	emit this->responseRequestChanged();
}

void AuthFlow::cancelAuthenticationRequest() {
	if (!this->currentSession) return;

	qCDebug(logPolkitState) << "cancelling authentication request by user request";

	this->mIsCancelled = true;
	this->currentSession->cancel();

	this->mRequest->cancel("Authentication request cancelled by user.");
}

void AuthFlow::setupSession() {
	delete this->currentSession;

	qCDebug(logPolkitState) << "setting up session for identity" << this->mSelectedIdentity->name();

	this->currentSession =
	    new Session(this->mSelectedIdentity->polkitIdentity.get(), this->mRequest->cookie, this);
	QObject::connect(this->currentSession, &Session::request, this, &AuthFlow::request);
	QObject::connect(this->currentSession, &Session::completed, this, &AuthFlow::completed);
	QObject::connect(this->currentSession, &Session::showError, this, &AuthFlow::showError);
	QObject::connect(this->currentSession, &Session::showInfo, this, &AuthFlow::showInfo);
	this->currentSession->initiate();
}

void AuthFlow::clearState() {
	this->mIsResponseRequired = false;
	this->mResponseMessage.clear();
	this->mResponseVisible = false;
	this->mExtraMessage.clear();
	this->mExtraIsError = false;

	emit this->responseRequestChanged();
	emit this->supplementaryChanged();
}

void AuthFlow::request(const QString& message, bool echo) {
	this->mIsResponseRequired = true;
	this->mResponseMessage = message;
	this->mResponseVisible = echo;
	emit this->responseRequestChanged();
}

void AuthFlow::completed(bool gainedAuthorization) {
	qCDebug(logPolkitState) << "authentication session completed, gainedAuthorization ="
	                        << gainedAuthorization << ", isCancelled =" << this->mIsCancelled;

	if (gainedAuthorization) {
		this->mIsCompleted = true;
		this->mIsSuccessful = true;
		this->mRequest->complete();

		emit this->completedChanged();
		emit this->authenticationSucceeded();
	} else if (this->mIsCancelled) {
		this->mIsCompleted = true;
		this->mIsSuccessful = false;

		emit this->completedChanged();
	} else {
		emit this->authenticationFailed();

		this->clearState();
		this->setupSession();
	}
}

void AuthFlow::showError(const QString& message) {
	this->mExtraMessage = message;
	this->mExtraIsError = true;
	emit this->supplementaryChanged();
}

void AuthFlow::showInfo(const QString& message) {
	this->mExtraMessage = message;
	this->mExtraIsError = false;
	emit this->supplementaryChanged();
}
} // namespace qs::service::polkit
