#include "qml.hpp"

#include "agentimpl.hpp"
#include "flow.hpp"

#include "../../core/generation.hpp"
#include "../../core/logcat.hpp"

namespace {
QS_LOGGING_CATEGORY(logPolkit, "quickshell.service.polkit");
}

namespace qs::service::polkit {
PolkitAgent::PolkitAgent(QObject* parent): PostReloadHook(parent) {}

PolkitAgent::~PolkitAgent() = default;

void PolkitAgent::classBegin() {
	// Nothing to do here.
}

void PolkitAgent::componentComplete() {
	this->PostReloadHook::componentComplete();

	if (this->mPath.isEmpty()) this->mPath = "/org/quickshell/Polkit";

	PolkitAgentImpl::tryGetOrCreate(this);
}

QString PolkitAgent::path() const { return this->mPath; }

void PolkitAgent::setPath(const QString& path) {
	if (this->mPath.isEmpty()) {
		this->mPath = path;
	} else if (this->mPath != path) {
		qCWarning(logPolkit) << "cannot change path after it has been set.";
	}
}

bool PolkitAgent::isRegistered() const {
	if (auto* impl = PolkitAgentImpl::tryGet(this); impl != nullptr) {
		return impl->isRegistered;
	}
	return false;
}

bool PolkitAgent::isActive() const {
	if (auto* impl = PolkitAgentImpl::tryGet(this); impl != nullptr) {
		return impl->activeFlow != nullptr;
	}
	return false;
}

AuthFlow* PolkitAgent::flow() const {
	if (auto* impl = PolkitAgentImpl::tryGet(this); impl != nullptr) {
		return impl->activeFlow;
	}
	return nullptr;
}

void PolkitAgent::onPostReload() {
	if (!PolkitAgentImpl::tryTakeover(this)) return;

	emit this->isRegisteredChanged();
	emit this->isActiveChanged();
	emit this->flowChanged();
}

} // namespace qs::service::polkit
