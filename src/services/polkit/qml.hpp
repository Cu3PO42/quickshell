#pragma once

#include <deque>

#include <polkit-qt6-1/polkitqt1-agent-listener.h>
#include <polkit-qt6-1/polkitqt1-agent-session.h>
#include <polkit-qt6-1/polkitqt1-details.h>
#include <polkit-qt6-1/polkitqt1-identity.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qqmlparserstatus.h>
#include <sys/types.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"

namespace qs::service::polkit {

//! All state that comes in from PolKit about an authentication request.
struct AuthRequest {
	//! The action ID that this session is for.
	QString actionId;
	//! Message to present to the user.
	QString message;
	//! Icon name according to the FreeDesktop specification. May be empty.
	QString iconName;
	// Details intentionally omitted because nothing seems to use them.
	QString cookie;
	//! List of users/groups that can be used for authentication.
	PolkitQt1::Identity::List identities;
	//! Implementation detail to mark authentication done.
	PolkitQt1::Agent::AsyncResult* result;
};

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

	/// The full path to a file containing an icon representing this identity, if available.
	Q_PROPERTY(QString icon READ icon CONSTANT);

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
	    QString icon,
	    bool isGroup,
	    PolkitQt1::Identity polkitIdentity,
	    QObject* parent = nullptr
	);
	~Identity() override;

	[[nodiscard]] id_t id() const;
	[[nodiscard]] const QString& name() const;
	[[nodiscard]] const QString& displayName() const;
	[[nodiscard]] const QString& icon() const;
	[[nodiscard]] bool isGroup() const;

	const PolkitQt1::Identity polkitIdentity;

private:
	id_t mId;
	QString mName;
	QString mDisplayName;
	QString mIcon;
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
class PolkitAgent
    : public PolkitQt1::Agent::Listener
    , public QQmlParserStatus {
	Q_OBJECT;
	QML_ELEMENT;
	Q_INTERFACES(QQmlParserStatus);

	// clang-format off
    /// The D-Bus path that this agent listener will use.
    ///
    /// > [!INFO] This value can be set to any valid path and has no impact on
    /// > the functionality of the listener.
    Q_PROPERTY(QString path READ path WRITE setPath REQUIRED);

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
	QSDOC_TYPE_OVERRIDE(ObjectModel<Identity>*)
	Q_PROPERTY(UntypedObjectModel* identities READ activeIdentities NOTIFY authenticationRequestStarted);

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

	/// Submit a response to a request that was previously emitted. Typically the password.
	Q_INVOKABLE void submit(const QString& value);

	/// Call this when the user wants to cancel/reject the authentication request.
	Q_INVOKABLE void cancelAuthenticationRequest();

	[[nodiscard]] QString path() const;
	void setPath(const QString& path);

	[[nodiscard]] bool isActive() const;
	[[nodiscard]] const QString& activeMessage() const;
	[[nodiscard]] const QString& activeIconName() const;
	[[nodiscard]] const QString& activeActionId() const;
	[[nodiscard]] ObjectModel<Identity>* activeIdentities();

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

	/// Emitted whenever @@isActive changes.
	void isActiveChanged();

	/// Emitted after the identity that is authenticating has changed by user request.
	void selectedIdentityChanged();

	/// Emitted whenever the authentication conversation generates a new message to present.
	///
	/// This is always additional to the authentictation request's base message.
	void subMessageChanged();

	/// Emitted when the user needs to provide input to continue authentication.
	void inputRequestChanged();

public slots:

private slots:
	// Implementation of the PolkitQt1::Agent::Listener interface.
	// We mark this private even though it's public in the base class since it
	// should not be called by anything other than PolkitQt1 itself.

	void initiateAuthentication(
	    const QString& actionId,
	    const QString& message,
	    const QString& iconName,
	    const PolkitQt1::Details& details,
	    const QString& cookie,
	    const PolkitQt1::Identity::List& identities,
	    PolkitQt1::Agent::AsyncResult* result
	) override;

	bool initiateAuthenticationFinish() override;
	void cancelAuthentication() override;

	// Signals received from session objects.

	void request(const QString& message, bool echo);
	void completed(bool gainedAuthorization);
	void showError(const QString& message);
	void showInfo(const QString& message);

private:
	void activateAuthenticationRequest();
	void setupSession();
	void finishAuthenticationRequest();

	QString mPath = "";
	ObjectModel<Identity> mIdentities {this};
	Identity* mSelectedIdentity = nullptr;

	InputRequest* mInputRequest = nullptr;
	SubMessage* mSubMessage = nullptr;

	std::deque<AuthRequest> queuedRequests;
	PolkitQt1::Agent::Session* currentSession = nullptr;

	bool isCancelled = false;
};

} // namespace qs::service::polkit
