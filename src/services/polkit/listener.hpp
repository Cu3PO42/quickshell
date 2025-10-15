#pragma once

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
// This causes a problem with variables of the name.
#undef signals

#include <glib-object.h>
#include <polkitagent/polkitagent.h>

#define signals Q_SIGNALS

namespace qs::service::polkit {
	class PolkitAgent;

	//! All state that comes in from PolKit about an authentication request.
	struct AuthRequest {
		~AuthRequest();

		//! The action ID that this session is for.
		QString actionId;
		//! Message to present to the user.
		QString message;
		//! Icon name according to the FreeDesktop specification. May be empty.
		QString iconName;
		// Details intentionally omitted because nothing seems to use them.
		QString cookie;
		//! List of users/groups that can be used for authentication.
		std::vector<PolkitIdentity *> identities;

		//! Implementation detail to mark authentication done.
		GTask* task;
		//! Implementation detail for requests cancelled by agent.
		GCancellable* cancellable;
		//! Callback handler ID for the cancellable.
		gulong handlerId;
		//! The agent that is handling this request.
		PolkitAgent* agent;

		void complete();
		void cancel(const QString& reason);

		void deleteLater();
	};
}

G_BEGIN_DECLS

#define QS_TYPE_POLKIT_AGENT (qs_polkit_agent_get_type())
G_DECLARE_FINAL_TYPE(QsPolkitAgent, qs_polkit_agent, QS, POLKIT_AGENT, PolkitAgentListener)

QsPolkitAgent* qs_polkit_agent_new(qs::service::polkit::PolkitAgent* agent);
void qs_polkit_agent_register(QsPolkitAgent* agent);
void qs_polkit_agent_unregister(QsPolkitAgent* agent);

G_END_DECLS
