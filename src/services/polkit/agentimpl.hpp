#pragma once

#include <deque>

#include <qobject.h>

#include "flow.hpp"
#include "gobjectref.hpp"
#include "listener.hpp"

namespace qs::service::polkit {
class PolkitAgent;

class PolkitAgentImpl
    : public QObject
    , public ListenerCb {
	Q_OBJECT;
	Q_DISABLE_COPY_MOVE(PolkitAgentImpl);

public:
	~PolkitAgentImpl() override;

	static PolkitAgentImpl* tryGetOrCreate(PolkitAgent* agent);
	static PolkitAgentImpl* tryGet(const PolkitAgent* agent);
	static PolkitAgentImpl* tryTakeover(PolkitAgent* agent);
	static void onEndOfQmlAgent(PolkitAgent* agent);

	void initiateAuthentication(AuthRequest* request) override;
	void cancelAuthentication(AuthRequest* request) override;
	void registerComplete(bool success) override;

private:
	PolkitAgentImpl(PolkitAgent* agent);

	static PolkitAgentImpl* instance;

	/// Start handling of the next authentication request in the queue.
	void activateAuthenticationRequest();
	/// Finalize and remove the current authentication request.
	void finishAuthenticationRequest();

	GObjectRef<QsPolkitAgent> listener;
	bool isRegistered = false;

	PolkitAgent* qmlAgent = nullptr;

	AuthFlow* activeFlow = nullptr;
	std::deque<AuthRequest*> queuedRequests;

	friend class PolkitAgent;
};
} // namespace qs::service::polkit
