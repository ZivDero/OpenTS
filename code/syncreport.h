/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

class EventClass;

/// <summary>
/// Writes the out-of-sync report for the current frame to disk.
/// </summary>
/// <param name="ev">The FRAMEINFO event whose checksum disagreed with ours, or NULL when the
/// report was asked for by the playback trap.</param>
/// <param name="crc_ring">The recent frame checksums, indexed by frame number masked to the
/// ring size.</param>
/// <param name="ring_size">The number of entries in the ring; a power of two.</param>
void Write_Sync_Report(EventClass const * ev, unsigned const * crc_ring, unsigned ring_size);

/// <summary>
/// Writes the report for a detected divergence, subject to a once-per-frame and per-session cap.
/// Leaves all session, connection and UI state untouched; the caller decides what to do next.
/// </summary>
/// <returns>bool; Was a report file written by this call?</returns>
bool Report_Out_Of_Sync(EventClass const & ev, unsigned const * crc_ring, unsigned ring_size);

/// <summary>
/// Clears the per-frame and per-session report guards at the start of a game.
/// </summary>
void Sync_Report_Reset(void);
