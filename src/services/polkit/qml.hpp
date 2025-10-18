#pragma once

#include <deque>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qqmlparserstatus.h>
#include <sys/types.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"
#include "../../core/reload.hpp"

typedef struct _PolkitIdentity PolkitIdentity;
typedef struct _QsPolkitAgent QsPolkitAgent;

namespace qs::service::polkit {

struct AuthRequest;
class Session;

//! Represents a user or group that can be used to authenticate.
class Identity: public QObject {
	Q_OBJECT;

	// clang-format off
	/// The Id of the identity. If the identity is a user, this is the user's uid. See @@isGroup.
	Q_PROPERTY(id_t id READ id CONSTANT);

	/// The name of the user or group.
	///
	/// If available, this is the actual username or group name, but may fallback to the ID.
	Q_PROPERTY(QString string READ name CONSTANT);

	/// The full name of the user or group, if available. Otherwise the same as @@name.
	Q_PROPERTY(QString displayName READ displayName CONSTANT);

	/// Indicates if this identity is a group or a user.
	///
	/// If true, @@id is a gid, otherwise it is a uid.
	Q_PROPERTY(bool isGroup READ isGroup CONSTANT);

	QML_UNCREATABLE("Identities cannot be created directly.");
	// clang-format on

public:
	explicit Identity(
	    id_t id,
	    QString name,
	    QString displayName,
	    bool isGroup,
	    PolkitIdentity* polkitIdentity,
	    QObject* parent = nullptr
	);
	~Identity() override;

	[[nodiscard]] id_t id() const;
	[[nodiscard]] const QString& name() const;
	[[nodiscard]] const QString& displayName() const;
	[[nodiscard]] bool isGroup() const;

	PolkitIdentity* polkitIdentity;

private:
	id_t mId;
	QString mName;
	QString mDisplayName;
	bool mIsGroup;
};

//! A message to present to the user as part of an authentication request.
class SubMessage: public QObject {
	Q_OBJECT;

	// clang-format off
	/// The text to present to the user.
	Q_PROPERTY(QString text READ text CONSTANT);

	/// If true, the message should be presented as an error.
	Q_PROPERTY(bool isError READ isError CONSTANT);

	QML_UNCREATABLE("SubMessages cannot be created directly. They are populated during authentication.");
	// clang-format on

public:
	explicit SubMessage(QString text, bool isError, QObject* parent = nullptr);
	~SubMessage() override;

	[[nodiscard]] const QString& text() const;
	[[nodiscard]] bool isError() const;

private:
	QString mText;
	bool mIsError;
};

//! A request for input from the user as part of an authentication request.
class InputRequest: public QObject {
	Q_OBJECT;

	// clang-format off
	/// The message to present to the user.
	Q_PROPERTY(QString message READ message CONSTANT);

	/// If the user's response should be visible. (e.g. for passwords this should be false)
	Q_PROPERTY(bool echo READ echo CONSTANT);

	QML_UNCREATABLE("InputRequests cannot be created directly. They are populated during authentication.");
	// clang-format on

public:
	explicit InputRequest(QString message, bool echo, QObject* parent = nullptr);
	~InputRequest() override;

	[[nodiscard]] const QString& message() const;
	[[nodiscard]] bool echo() const;

private:
	QString mMessage;
	bool mEcho;
};

//! Contains interface to instantiate a PolKit agent listener.
class PolkitAgent: public PostReloadHook {
	Q_OBJECT;
	QML_ELEMENT;

	// clang-format off
    /// The D-Bus path that this agent listener will use.
	///
	/// If not set, a default of /org/quickshell/Polkit will be used.
    Q_PROPERTY(QString path READ path WRITE setPath);

	/// Indicates whether the agent registered successfully and is in use.
	Q_PROPERTY(bool isRegistered READ isRegistered NOTIFY isRegisteredChanged);

	/// Indicates an ongoing authentication request.
	///
	/// If this is true, other properties such as @@message and @@iconName will
	/// also be populated with relevant information.
	Q_PROPERTY(bool isActive READ isActive NOTIFY isActiveChanged);

	/// The authentication message to present to the user.
	Q_PROPERTY(QString message READ activeMessage NOTIFY authenticationRequestStarted);

	/// The icon name to present to the user in association with the message.
	///
	/// The icon name follows the [FreeDesktop icon naming specification](https://specifications.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html).
	Q_PROPERTY(QString iconName READ activeIconName NOTIFY authenticationRequestStarted);

	/// The action ID that this authentication request is for.
	Q_PROPERTY(QString actionId READ activeActionId NOTIFY authenticationRequestStarted);

	/// The list of identities that may be used to authenticate.
	///
	/// Each identity may be a user or a group. You may select any of them to
	/// authenticate by setting @@selectedIdentity. By default, the first identity
	/// in the list is selected.
	Q_PROPERTY(QList<Identity*> identities READ activeIdentities NOTIFY authenticationRequestStarted);

	/// The identity that will be used to authenticate.
	/// 
	/// Setting this will abort any ongoing authentication conversations and start a new one.
	Q_PROPERTY(Identity* selectedIdentity READ selectedIdentity WRITE setSelectedIdentity NOTIFY selectedIdentityChanged);

	/// An input request that is posed to the user as part of an authentication reqeust.
	///
	/// To reply, call @@submit() with the user's input.
	Q_PROPERTY(InputRequest* inputRequest READ inputRequest NOTIFY inputRequestChanged);

	/// An additional message to present to the user.
	///
	/// This message is added dynamically during the authentication process.
	/// It may be used to present errors such as 'invalid password'.
	Q_PROPERTY(SubMessage* subMessage READ subMessage NOTIFY subMessageChanged);
	// clang-format on

public:
	explicit PolkitAgent(QObject* parent = nullptr);
	~PolkitAgent() override;

	void classBegin() override;
	void componentComplete() override;

	/// Incoming request from the PolKit daemon to authenticate an action.
	void initiateAuthentication(AuthRequest* request);
	/// Cancel authentication for a specific request from daemon.
	void cancelAuthentication(AuthRequest* request);
	/// Called from our listener when registration of the agent is complete.
	void registerComplete(bool success);

	/// Submit a response to a request that was previously emitted. Typically the password.
	Q_INVOKABLE void submit(const QString& value);

	/// Call this when the user wants to cancel/reject the authentication request.
	Q_INVOKABLE void cancelAuthenticationRequest();

	[[nodiscard]] QString path() const;
	void setPath(const QString& path);

	[[nodiscard]] bool isRegistered() const;
	[[nodiscard]] bool isActive() const;
	[[nodiscard]] const QString& activeMessage() const;
	[[nodiscard]] const QString& activeIconName() const;
	[[nodiscard]] const QString& activeActionId() const;
	[[nodiscard]] const QList<Identity*>& activeIdentities() const;

	[[nodiscard]] Identity* selectedIdentity() const;
	void setSelectedIdentity(Identity* identity);

	[[nodiscard]] InputRequest* inputRequest() const;
	[[nodiscard]] SubMessage* subMessage() const;

signals:
	/// Emitted when an application makes a request that requires authentication.
	///
	/// At this point, @@isActive will be true and other properties such as
	/// @@message and @@iconName will be populated with relevant information.
	void authenticationRequestStarted();

	/// Emmitted when on ongoing authentication request is cancelled by the PolKit daemon.
	void authenticationRequestCancelled();

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

	/// Emitted whenever an authentication request completes successfully.
	void authenticationSucceeded();

	void isRegisteredChanged();
	void isActiveChanged();
	void selectedIdentityChanged();
	void subMessageChanged();
	void inputRequestChanged();

private slots:
	// Signals received from session objects.

	void request(const QString& message, bool echo);
	void completed(bool gainedAuthorization);
	void showError(const QString& message);
	void showInfo(const QString& message);

protected:
	void onPostReload() override;

private:
	/// Start handling of the next authentication request in the queue.
	void activateAuthenticationRequest();
	/// Start a session for the currently selected identity and the current request.
	void setupSession();
	/// Finalize and remove the current authentication request.
	void finishAuthenticationRequest();

	QString mPath = "";
	bool mIsRegistered = false;
	bool isRegistering = false;
	QList<Identity*> mIdentities;
	Identity* mSelectedIdentity = nullptr;

	InputRequest* mInputRequest = nullptr;
	SubMessage* mSubMessage = nullptr;

	QsPolkitAgent* listener = nullptr;
	std::deque<AuthRequest*> queuedRequests;
	Session* currentSession = nullptr;

	bool isCancelled = false;
};

} // namespace qs::service::polkit
