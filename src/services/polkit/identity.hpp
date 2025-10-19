#pragma once

#include <qobject.h>
#include <qqmlintegration.h>

using PolkitIdentity = struct _PolkitIdentity;

namespace qs::service::polkit {
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

	static Identity* fromPolkitIdentity(PolkitIdentity* identity);

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
} // namespace qs::service::polkit
