#include "qml.hpp"

#include <qloggingcategory.h>
#include <qobject.h>
#include <qtmetamacros.h>

#include "../../core/logcat.hpp"
#include "agentimpl.hpp"
#include "flow.hpp"

namespace {
QS_LOGGING_CATEGORY(logPolkit, "quickshell.service.polkit");
}

namespace qs::service::polkit {
PolkitAgent::PolkitAgent(QObject* parent): QObject(parent) {}

void PolkitAgent::componentComplete() {
	if (this->mPath.isEmpty()) this->mPath = "/org/quickshell/Polkit";

	PolkitAgentImpl::tryTakeover(this);

	emit this->isRegisteredChanged();
	emit this->isActiveChanged();
	emit this->flowChanged();
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
} // namespace qs::service::polkit
