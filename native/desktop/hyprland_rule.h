#pragma once

#include <QString>

/// Install the named Hyprland rule that floats Jot and places it at the
/// top-right before its first map. Safe to call on every launch: the named
/// rule is replaced rather than stacked.
bool installJotPlacementRule(const QString &hyprctl, QString *error);
