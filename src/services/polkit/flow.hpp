#pragma once

#include <qobject.h>
#include <qqmlintegration.h>

#include "../../core/retainable.hpp"
#include "identity.hpp"
#include "listener.hpp"

namespace qs::service::polkit {
class Session;

class AuthFlow
    : public QObject
    , public Retainable {
	Q_OBJECT;
	QML_ELEMENT;
	Q_DISABLE_COPY_MOVE(AuthFlow);
	QML_UNCREATABLE("AuthFlow can only be obtained from PolkitAgent.");

	// clang-format off
	/// The main message to present to the user.
	Q_PROPERTY(QString message READ message CONSTANT);

	/// The icon to present to the user in association with the message.
	///
	/// The icon name follows the [FreeDesktop icon naming specification](https://specifications.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html).
	/// Use @@Quickshell.Quickshell.iconPath() to resolve the icon name to an
	/// actual file path for display.
	Q_PROPERTY(QString iconName READ iconName CONSTANT);

	/// The action ID represents the action that is being authorized.
	///
	/// This is a machine-readable identifier.
	Q_PROPERTY(QString actionId READ actionId CONSTANT);

	/// A cookie that identifies this authentication request.
	///
	/// This is an internal identifier and not recommended to show to users.
	Q_PROPERTY(QString cookie READ cookie CONSTANT);

	/// The list of identities that may be used to authenticate.
	///
	/// Each identity may be a user or a group. You may select any of them to
	/// authenticate by setting @@selectedIdentity. By default, the first identity
	/// in the list is selected.
	Q_PROPERTY(QList<Identity*> identities READ identities CONSTANT);

	/// The identity that will be used to authenticate.
	///
	/// Changing this will abort any ongoing authentication conversations and start a new one.
	Q_PROPERTY(Identity* selectedIdentity READ selectedIdentity WRITE setSelectedIdentity NOTIFY selectedIdentityChanged);

	/// Indicates that a response from the user is required from the user,
	/// typically a password.
	Q_PROPERTY(bool isResponseRequired READ isResponseRequired NOTIFY responseRequestChanged);

	/// This message is used to prompt the user for required input.
	Q_PROPERTY(QString inputPrompt READ inputPrompt NOTIFY responseRequestChanged);

	/// Indicates whether the user's response should be visible. (e.g. for passwords this should be false)
	Q_PROPERTY(bool responseVisible READ responseVisible NOTIFY responseRequestChanged);

	/// An additional message to present to the user.
	///
	/// This may be used to show errors or supplementary information.
	/// See @@supplementaryIsError to determine if this is an error message.
	Q_PROPERTY(QString supplementaryMessage READ supplementaryMessage NOTIFY supplementaryChanged);

	/// Indicates whether the supplementary message is an error.
	Q_PROPERTY(bool supplementaryIsError READ supplementaryIsError NOTIFY supplementaryChanged);

	/// Has the authentication request been completed.
	Q_PROPERTY(bool isCompleted READ isCompleted NOTIFY completedChanged);

	/// Indicates whether the authentication request was successful.
	Q_PROPERTY(bool isSuccessful READ isSuccessful NOTIFY completedChanged);
	// clang-format on

public:
	explicit AuthFlow(AuthRequest* request, QList<Identity*>&& identities, QObject* parent = nullptr);
	~AuthFlow() override;

	/// Cancel the ongoing authentication request from the agent side.
	void cancelFromAgent();

	/// Submit a response to a request that was previously emitted. Typically the password.
	Q_INVOKABLE void submit(const QString& value);
	/// Cancel the ongoing authentication request from the user side.
	Q_INVOKABLE void cancelAuthenticationRequest();

	[[nodiscard]] const QString& message() const;
	[[nodiscard]] const QString& iconName() const;
	[[nodiscard]] const QString& actionId() const;
	[[nodiscard]] const QString& cookie() const;
	[[nodiscard]] const QList<Identity*>& identities() const;

	[[nodiscard]] Identity* selectedIdentity() const;
	void setSelectedIdentity(Identity* identity);

	[[nodiscard]] bool isResponseRequired() const;
	[[nodiscard]] const QString& inputPrompt() const;
	[[nodiscard]] bool responseVisible() const;

	[[nodiscard]] const QString& supplementaryMessage() const;
	[[nodiscard]] bool supplementaryIsError() const;

	[[nodiscard]] bool isCompleted() const;
	[[nodiscard]] bool isSuccessful() const;

	[[nodiscard]] AuthRequest* authRequest() const;

signals:
	/// Emitted whenever an authentication request completes successfully.
	void authenticationSucceeded();

	/// Emitted whenever an authentication request completes unsuccessfully.
	///
	/// This may be because the user entered the wrong password or otherwise
	/// failed to authenticate.
	/// This signal is not emmitted when the user canceled the request or it
	/// was cancelled by the PolKit daemon.
	///
	/// After this signal, a new session is automatically started for the same
	/// identity.
	void authenticationFailed();

	/// Emmitted when on ongoing authentication request is cancelled by the PolKit daemon.
	void authenticationRequestCancelled();

	void selectedIdentityChanged();
	void responseRequestChanged();
	void supplementaryChanged();
	void completedChanged();

private slots:
	// Signals received from session objects.
	void request(const QString& message, bool echo);
	void completed(bool gainedAuthorization);
	void showError(const QString& message);
	void showInfo(const QString& message);

private:
	/// Start a session for the currently selected identity and the current request.
	void setupSession();
	/// Clear all state variables.
	void clearState();

	AuthRequest* mRequest = nullptr;
	QList<Identity*> mIdentities;
	Identity* mSelectedIdentity = nullptr;
	bool mIsResponseRequired = false;
	QString mResponseMessage;
	bool mResponseVisible = false;
	QString mExtraMessage;
	bool mExtraIsError = false;
	bool mIsCompleted = false;
	bool mIsSuccessful = false;
	bool mIsCancelled = false;

	Session* currentSession = nullptr;

	friend class PolkitAgent;
};

} // namespace qs::service::polkit
