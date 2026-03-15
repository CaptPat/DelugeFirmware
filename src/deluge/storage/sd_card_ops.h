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

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <variant>

extern "C" {
#include "fatfs/ff.h"
}

/// Deferred SD card operation system.
///
/// Prevents re-entrant FatFS access by queueing non-critical SD operations
/// (file deletes, renames, settings writes) when a higher-level SD operation
/// is already in progress. Queued operations execute when the current
/// operation completes.
///
/// Audio cluster loading is unaffected — it operates below this layer.
namespace SDCardOps {

// --- Deferrable operation types ---

struct UnlinkOp {
	char path[FF_MAX_LFN + 1];
};

struct RenameOp {
	char pathOld[FF_MAX_LFN + 1];
	char pathNew[FF_MAX_LFN + 1];
};

/// Coalesced settings write — calls MIDIDeviceManager::writeDevicesToFile()
/// and runtimeFeatureSettings.writeSettingsToFile(). Multiple requests
/// before drain collapse into a single write.
struct SettingsWriteOp {};

using DeferredOp = std::variant<UnlinkOp, RenameOp, SettingsWriteOp>;

// --- Atomic SD card lock ---
//
// Any code that wants to access the SD card (reads or writes) must acquire
// the lock first via tryAcquire(). This replaces the `inCardRoutine`
// parameter that was previously threaded through ~60 UI call sites.
//
// - Read callers: call tryAcquire(). If it returns false, the card is busy —
//   return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE (same behavior as
//   today, but the check is at the lock, not a passed-in parameter).
// - Write callers: use the requestUnlink/requestRename/requestSettingsWrite
//   API below, which auto-defers if the lock is held.

/// Attempt to acquire exclusive SD card access. Returns true if acquired.
/// Uses atomic test-and-set — safe to call from any context.
bool tryAcquire();

/// Release SD card access. Drains any queued deferred operations.
void release();

/// Returns true if the SD card lock is currently held.
bool isLocked();

// --- Defer-or-execute API ---
//
// If no operation is in progress, executes immediately and returns the result.
// If one is in progress, queues the operation for later execution.
// Queued operations return FR_OK (the real result comes at drain time).

/// Delete a file. Deferred if an SD operation is in progress.
FRESULT requestUnlink(const char* path);

/// Rename/move a file. Deferred if an SD operation is in progress.
FRESULT requestRename(const char* pathOld, const char* pathNew);

/// Write settings files (MIDI devices + runtime features).
/// Multiple requests coalesce into one. Deferred if an SD operation is in progress.
void requestSettingsWrite();

} // namespace SDCardOps
