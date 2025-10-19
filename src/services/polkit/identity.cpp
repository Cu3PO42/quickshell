#include "identity.hpp"

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
// Workaround macro collision with glib 'signals' struct member.
#undef signals
#include <polkit/polkit.h>
#include <polkitagent/polkitagent.h>
#define signals Q_SIGNALS
#include <grp.h>
#include <pwd.h>

namespace qs::service::polkit {
Identity::Identity(
    id_t id,
    QString name,
    QString displayName,
    bool isGroup,
    PolkitIdentity* polkitIdentity,
    QObject* parent
)
    : QObject(parent)
    , polkitIdentity(polkitIdentity)
    , mId(id)
    , mName(std::move(name))
    , mDisplayName(std::move(displayName))
    , mIsGroup(isGroup) {}

Identity::~Identity() = default;

Identity* Identity::fromPolkitIdentity(PolkitIdentity* identity) {
	if (POLKIT_IS_UNIX_USER(identity)) {
		auto uid = polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity));
		auto* pw = getpwuid(uid);
		auto name =
		    (pw && pw->pw_name && *pw->pw_name) ? QString::fromUtf8(pw->pw_name) : QString::number(uid);
		return new Identity(
		    uid,
		    name,
		    (pw && pw->pw_gecos && *pw->pw_gecos) ? QString::fromUtf8(pw->pw_gecos) : name,
		    false,
		    identity
		);
	}

	if (POLKIT_IS_UNIX_GROUP(identity)) {
		auto gid = polkit_unix_group_get_gid(POLKIT_UNIX_GROUP(identity));
		auto* gr = getgrgid(gid);
		auto name =
		    (gr && gr->gr_name && *gr->gr_name) ? QString::fromUtf8(gr->gr_name) : QString::number(gid);
		return new Identity(gid, name, name, true, identity);
	}

	// A different type of identity is netgroup.
	return nullptr;
}

id_t Identity::id() const { return this->mId; }
const QString& Identity::name() const { return this->mName; }
const QString& Identity::displayName() const { return this->mDisplayName; }
bool Identity::isGroup() const { return this->mIsGroup; }

} // namespace qs::service::polkit
