#pragma once

#include <deque>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qqmlparserstatus.h>
#include <sys/types.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"
#include "../../core/reload.hpp"
#include "../../core/retainable.hpp"

// The reserved identifier is exactly the struct I mean.
using PolkitIdentity = struct _PolkitIdentity; // NOLINT(bugprone-reserved-identifier)
using QsPolkitAgent = struct _QsPolkitAgent;

namespace qs::service::polkit {

struct AuthRequest;
class Session;

class Identity;
class AuthFlow;

//! Contains interface to instantiate a PolKit agent listener.
class PolkitAgent
    : public QObject
    , public QQmlParserStatus {
	Q_OBJECT;
	QML_ELEMENT;
	Q_INTERFACES(QQmlParserStatus);
	Q_DISABLE_COPY_MOVE(PolkitAgent);

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

	/// The current authentication state if an authentication request is active.
	/// Null when no authentication request is active.
	Q_PROPERTY(AuthFlow* flow READ flow NOTIFY flowChanged);
	// clang-format on

public:
	explicit PolkitAgent(QObject* parent = nullptr);
	~PolkitAgent() override = default;

	void classBegin() override {};
	void componentComplete() override;

	[[nodiscard]] QString path() const;
	void setPath(const QString& path);

	[[nodiscard]] bool isRegistered() const;
	[[nodiscard]] bool isActive() const;
	[[nodiscard]] AuthFlow* flow() const;

signals:
	/// Emitted when an application makes a request that requires authentication.
	///
	/// At this point, @@state will be populated with relevant information.
	/// Note that signals for conversation outcome are emitted from the @@AuthFlow instance.
	void authenticationRequestStarted();

	void isRegisteredChanged();
	void isActiveChanged();
	void flowChanged();

private:
	QString mPath = "";
};
} // namespace qs::service::polkit
