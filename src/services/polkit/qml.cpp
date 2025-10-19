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
	PostReloadHook::componentComplete();

	if (mPath.isEmpty()) mPath = "/org/quickshell/Polkit";

	PolkitAgentImpl::tryGetOrCreate(this);
}

QString PolkitAgent::path() const { return mPath; }

void PolkitAgent::setPath(const QString& path) {
	if (mPath.isEmpty()) {
		mPath = path;
	} else if (mPath != path) {
		qCWarning(logPolkit) << "cannot change path after it has been set.";
	}
}

bool PolkitAgent::isRegistered() const {
	if (auto impl = PolkitAgentImpl::tryGet(this); impl != nullptr) {
		return impl->isRegistered;
	}
	return false;
}

bool PolkitAgent::isActive() const {
	if (auto impl = PolkitAgentImpl::tryGet(this); impl != nullptr) {
		return impl->activeFlow != nullptr;
	}
	return false;
}

AuthFlow* PolkitAgent::flow() const {
	if (auto impl = PolkitAgentImpl::tryGet(this); impl != nullptr) {
		return impl->activeFlow;
	}
	return nullptr;
}

void PolkitAgent::onPostReload() {
	if (!PolkitAgentImpl::tryTakeover(this)) return;

	emit isRegisteredChanged();
	emit isActiveChanged();
	emit flowChanged();
}

} // namespace qs::service::polkit
