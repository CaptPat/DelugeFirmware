/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "storage/sd_card_ops.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "model/settings/runtime_feature_settings.h"
#include <cstring>

extern "C" {
#include "fatfs/ff.h"
}

namespace SDCardOps {

namespace {

int32_t operationDepth = 0;
bool settingsWritePending = false;

DeferredOp deferredQueue[kMaxDeferredOps];
size_t deferredCount = 0;

void executeDeferredOp(const DeferredOp& op) {
	std::visit(
	    [](auto&& arg) {
		    using T = std::decay_t<decltype(arg)>;
		    if constexpr (std::is_same_v<T, UnlinkOp>) {
			    f_unlink(arg.path);
		    }
		    else if constexpr (std::is_same_v<T, RenameOp>) {
			    f_rename(arg.pathOld, arg.pathNew);
		    }
		    else if constexpr (std::is_same_v<T, SettingsWriteOp>) {
			    MIDIDeviceManager::writeDevicesToFile();
			    runtimeFeatureSettings.writeSettingsToFile();
		    }
	    },
	    op);
}

void drainQueue() {
	// Execute all queued ops. We don't re-set operationInProgress here
	// because drain runs after endOperation has already decremented depth to 0,
	// and these deferred ops are small/fast (single FatFS calls).
	for (size_t i = 0; i < deferredCount; i++) {
		executeDeferredOp(deferredQueue[i]);
	}
	deferredCount = 0;

	// Handle coalesced settings write
	if (settingsWritePending) {
		settingsWritePending = false;
		MIDIDeviceManager::writeDevicesToFile();
		runtimeFeatureSettings.writeSettingsToFile();
	}
}

bool enqueue(const DeferredOp& op) {
	if (deferredCount >= kMaxDeferredOps) {
		// Queue full — drop the operation. In debug builds we'd want to know.
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E500");
#endif
		return false;
	}
	deferredQueue[deferredCount++] = op;
	return true;
}

} // anonymous namespace

void beginOperation() {
	operationDepth++;
}

void endOperation() {
	if (operationDepth > 0) {
		operationDepth--;
	}
	if (operationDepth == 0) {
		drainQueue();
	}
}

bool operationInProgress() {
	return operationDepth > 0;
}

FRESULT requestUnlink(const char* path) {
	if (!operationInProgress()) {
		return f_unlink(path);
	}
	UnlinkOp op;
	strncpy(op.path, path, FF_MAX_LFN);
	op.path[FF_MAX_LFN] = '\0';
	enqueue(op);
	return FR_OK;
}

FRESULT requestRename(const char* pathOld, const char* pathNew) {
	if (!operationInProgress()) {
		return f_rename(pathOld, pathNew);
	}
	RenameOp op;
	strncpy(op.pathOld, pathOld, FF_MAX_LFN);
	op.pathOld[FF_MAX_LFN] = '\0';
	strncpy(op.pathNew, pathNew, FF_MAX_LFN);
	op.pathNew[FF_MAX_LFN] = '\0';
	enqueue(op);
	return FR_OK;
}

void requestSettingsWrite() {
	if (!operationInProgress()) {
		MIDIDeviceManager::writeDevicesToFile();
		runtimeFeatureSettings.writeSettingsToFile();
		return;
	}
	// Coalesce — just set the flag, don't consume a queue slot
	settingsWritePending = true;
}

} // namespace SDCardOps
