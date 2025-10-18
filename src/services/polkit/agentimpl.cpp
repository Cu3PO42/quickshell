#include "agentimpl.hpp"

#include "../../core/generation.hpp"
#include "../../core/logcat.hpp"
#include "qml.hpp"

namespace {
QS_LOGGING_CATEGORY(logPolkit, "quickshell.service.polkit");
}

namespace qs::service::polkit {
PolkitAgentImpl* PolkitAgentImpl::instance = nullptr;

PolkitAgentImpl::PolkitAgentImpl(PolkitAgent* agent): QObject(nullptr), qmlAgent(agent) {
	auto path = qmlAgent->path().toUtf8();
	listener = qs_polkit_agent_new(this);
	qs_polkit_agent_register(listener, path.constData());
}

PolkitAgentImpl::~PolkitAgentImpl() {
	for (; !queuedRequests.empty(); queuedRequests.pop_back()) {
		AuthRequest* req = queuedRequests.back();
		qCDebug(logPolkit) << "destroying queued authentication request for action" << req->actionId;
		req->cancel("PolkitAgent is being destroyed");
		delete req;
	}

	if (activeFlow) {
		activeFlow->cancelAuthenticationRequest();
		activeFlow->deleteLater();
	}

	if (isRegistered) qs_polkit_agent_unregister(listener);
	g_object_unref(listener);
}

PolkitAgentImpl* PolkitAgentImpl::tryGetOrCreate(PolkitAgent* agent) {
	if (instance == nullptr) instance = new PolkitAgentImpl(agent);
	if (instance->qmlAgent == agent) return instance;
	return nullptr;
}

PolkitAgentImpl* PolkitAgentImpl::tryGet(const PolkitAgent* agent) {
	if (instance == nullptr) return nullptr;
	if (instance->qmlAgent == agent) return instance;
	return nullptr;
}

PolkitAgentImpl* PolkitAgentImpl::tryTakeover(PolkitAgent* agent) {
	if (auto impl = tryGet(agent); impl != nullptr) return impl;

	auto prevGen = EngineGeneration::findObjectGeneration(instance->qmlAgent);
	auto myGen = EngineGeneration::findObjectGeneration(agent);
	if (prevGen == myGen) return nullptr;

	qCDebug(logPolkit) << "taking over listener from previous generation";

	if (instance->qmlAgent->path() != agent->path()) {
		qCWarning(logPolkit) << "path differs from previous generation, change will not take effect";
	}

	instance->qmlAgent = agent;
	emit agent->isRegisteredChanged();

	return instance;
}

void PolkitAgentImpl::onEndOfQmlAgent(PolkitAgent* agent) {
	if (instance != nullptr && instance->qmlAgent == agent) {
		delete instance;
		instance = nullptr;
	}
}

void PolkitAgentImpl::registerComplete(bool success) {
	if (success) {
		isRegistered = true;
		emit qmlAgent->isRegisteredChanged();
	} else {
		qCWarning(logPolkit) << "failed to register listener on path" << qmlAgent->path();
	}
}

void PolkitAgentImpl::initiateAuthentication(AuthRequest* request) {
	qCDebug(logPolkit) << "incoming authentication request for action" << request->actionId;

	queuedRequests.emplace_back(request);

	if (queuedRequests.size() == 1) {
		activateAuthenticationRequest();
	}
}

void PolkitAgentImpl::cancelAuthentication(AuthRequest* request) {
	qCDebug(logPolkit) << "cancelling authentication request from agent";

	if (activeFlow && activeFlow->authRequest() == request) {
		activeFlow->cancelFromAgent();
	} else if (auto it = std::find(queuedRequests.begin(), queuedRequests.end(), request);
	           it != queuedRequests.end())
	{
		qCDebug(logPolkit) << "removing queued authentication request for action" << (*it)->actionId;
		(*it)->cancel("Authentication request was cancelled");
		delete (*it);
		queuedRequests.erase(it);
	} else {
		qCWarning(logPolkit) << "the cancelled request was not found in the queue.";
	}
}

void PolkitAgentImpl::activateAuthenticationRequest() {
	if (queuedRequests.empty()) return;

	AuthRequest* req = queuedRequests.front();
	queuedRequests.pop_front();
	qCDebug(logPolkit) << "activating authentication request for action" << req->actionId
	                   << ", cookie: " << req->cookie;

	QList<Identity*> identities;
	for (auto identity: req->identities) {
		auto obj = Identity::fromPolkitIdentity(identity);
		if (obj) identities.append(obj);
	}
	if (identities.isEmpty()) {
		qCWarning(logPolkit
		) << "no supported identities available for authentication request, cancelling.";
		req->cancel("Error requesting authentication: no supported identities available.");
		delete req;
		return;
	}

	activeFlow = new AuthFlow(req, std::move(identities));

	QObject::connect(
	    activeFlow,
	    &AuthFlow::completedChanged,
	    this,
	    &PolkitAgentImpl::finishAuthenticationRequest
	);

	emit qmlAgent->isActiveChanged();
	emit qmlAgent->flowChanged();
	emit qmlAgent->authenticationRequestStarted();
}

void PolkitAgentImpl::finishAuthenticationRequest() {
	if (!activeFlow) return;

	qCDebug(logPolkit) << "finishing authentication request for action" << activeFlow->actionId();

	activeFlow->deleteLater();
	activeFlow = nullptr;
	emit qmlAgent->flowChanged();

	if (!queuedRequests.empty()) {
		activateAuthenticationRequest();
	} else {
		emit qmlAgent->isActiveChanged();
	}
}
} // namespace qs::service::polkit
