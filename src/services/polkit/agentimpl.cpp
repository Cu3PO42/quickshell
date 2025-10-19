#include "agentimpl.hpp"
#include <algorithm>
#include <utility>

#include <glib-object.h>
#include <qlist.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qtmetamacros.h>

#include "../../core/generation.hpp"
#include "../../core/logcat.hpp"
#include "listener.hpp"
#include "qml.hpp"

namespace {
QS_LOGGING_CATEGORY(logPolkit, "quickshell.service.polkit");
}

namespace qs::service::polkit {
PolkitAgentImpl* PolkitAgentImpl::instance = nullptr;

PolkitAgentImpl::PolkitAgentImpl(PolkitAgent* agent)
    : QObject(nullptr)
    , listener(qs_polkit_agent_new(this))
    , qmlAgent(agent) {
	auto path = this->qmlAgent->path().toUtf8();
	qs_polkit_agent_register(this->listener, path.constData());
}

PolkitAgentImpl::~PolkitAgentImpl() {
	for (; !this->queuedRequests.empty(); this->queuedRequests.pop_back()) {
		AuthRequest* req = this->queuedRequests.back();
		qCDebug(logPolkit) << "destroying queued authentication request for action" << req->actionId;
		req->cancel("PolkitAgent is being destroyed");
		delete req;
	}

	if (this->activeFlow) {
		this->activeFlow->cancelAuthenticationRequest();
		this->activeFlow->deleteLater();
	}

	if (this->isRegistered) qs_polkit_agent_unregister(this->listener);
	g_object_unref(this->listener);
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
	if (auto* impl = tryGet(agent); impl != nullptr) return impl;

	auto* prevGen = EngineGeneration::findObjectGeneration(instance->qmlAgent);
	auto* myGen = EngineGeneration::findObjectGeneration(agent);
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
		this->isRegistered = true;
		emit this->qmlAgent->isRegisteredChanged();
	} else {
		qCWarning(logPolkit) << "failed to register listener on path" << this->qmlAgent->path();
	}
}

void PolkitAgentImpl::initiateAuthentication(AuthRequest* request) {
	qCDebug(logPolkit) << "incoming authentication request for action" << request->actionId;

	this->queuedRequests.emplace_back(request);

	if (this->queuedRequests.size() == 1) {
		this->activateAuthenticationRequest();
	}
}

void PolkitAgentImpl::cancelAuthentication(AuthRequest* request) {
	qCDebug(logPolkit) << "cancelling authentication request from agent";

	if (this->activeFlow && this->activeFlow->authRequest() == request) {
		this->activeFlow->cancelFromAgent();
	} else if (auto it = std::ranges::find(this->queuedRequests, request);
	           it != this->queuedRequests.end())
	{
		qCDebug(logPolkit) << "removing queued authentication request for action" << (*it)->actionId;
		(*it)->cancel("Authentication request was cancelled");
		delete (*it);
		this->queuedRequests.erase(it);
	} else {
		qCWarning(logPolkit) << "the cancelled request was not found in the queue.";
	}
}

void PolkitAgentImpl::activateAuthenticationRequest() {
	if (this->queuedRequests.empty()) return;

	AuthRequest* req = this->queuedRequests.front();
	this->queuedRequests.pop_front();
	qCDebug(logPolkit) << "activating authentication request for action" << req->actionId
	                   << ", cookie: " << req->cookie;

	QList<Identity*> identities;
	for (auto* identity: req->identities) {
		auto* obj = Identity::fromPolkitIdentity(identity);
		if (obj) identities.append(obj);
	}
	if (identities.isEmpty()) {
		qCWarning(logPolkit
		) << "no supported identities available for authentication request, cancelling.";
		req->cancel("Error requesting authentication: no supported identities available.");
		delete req;
		return;
	}

	this->activeFlow = new AuthFlow(req, std::move(identities));

	QObject::connect(
	    this->activeFlow,
	    &AuthFlow::completedChanged,
	    this,
	    &PolkitAgentImpl::finishAuthenticationRequest
	);

	emit this->qmlAgent->isActiveChanged();
	emit this->qmlAgent->flowChanged();
	emit this->qmlAgent->authenticationRequestStarted();
}

void PolkitAgentImpl::finishAuthenticationRequest() {
	if (!this->activeFlow) return;

	qCDebug(logPolkit) << "finishing authentication request for action"
	                   << this->activeFlow->actionId();

	this->activeFlow->deleteLater();
	this->activeFlow = nullptr;
	emit this->qmlAgent->flowChanged();

	if (!this->queuedRequests.empty()) {
		this->activateAuthenticationRequest();
	} else {
		emit this->qmlAgent->isActiveChanged();
	}
}
} // namespace qs::service::polkit
