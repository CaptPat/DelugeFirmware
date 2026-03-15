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
#include <atomic>
#include <cstring>

extern "C" {
#include "fatfs/ff.h"
}

namespace SDCardOps {

namespace {

constexpr size_t kMaxDeferredOps = 8;

std::atomic_flag sdLock = ATOMIC_FLAG_INIT;
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
	// Execute all queued ops. Called after release() clears the lock.
	// These deferred ops are small/fast (single FatFS calls).
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

bool tryAcquire() {
	return !sdLock.test_and_set(std::memory_order_acquire);
}

void release() {
	sdLock.clear(std::memory_order_release);
	drainQueue();
}

bool isLocked() {
	// Try to acquire — if we can, it wasn't locked, so clear and return false.
	// If we can't, it was locked.
	if (!sdLock.test_and_set(std::memory_order_acquire)) {
		sdLock.clear(std::memory_order_release);
		return false;
	}
	return true;
}

FRESULT requestUnlink(const char* path) {
	if (tryAcquire()) {
		FRESULT result = f_unlink(path);
		release();
		return result;
	}
	// Lock held — defer the operation
	UnlinkOp op;
	strncpy(op.path, path, FF_MAX_LFN);
	op.path[FF_MAX_LFN] = '\0';
	enqueue(op);
	return FR_OK;
}

FRESULT requestRename(const char* pathOld, const char* pathNew) {
	if (tryAcquire()) {
		FRESULT result = f_rename(pathOld, pathNew);
		release();
		return result;
	}
	// Lock held — defer the operation
	RenameOp op;
	strncpy(op.pathOld, pathOld, FF_MAX_LFN);
	op.pathOld[FF_MAX_LFN] = '\0';
	strncpy(op.pathNew, pathNew, FF_MAX_LFN);
	op.pathNew[FF_MAX_LFN] = '\0';
	enqueue(op);
	return FR_OK;
}

void requestSettingsWrite() {
	if (tryAcquire()) {
		MIDIDeviceManager::writeDevicesToFile();
		runtimeFeatureSettings.writeSettingsToFile();
		release();
		return;
	}
	// Lock held — coalesce, don't consume a queue slot
	settingsWritePending = true;
}

} // namespace SDCardOps
