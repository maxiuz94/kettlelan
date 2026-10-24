// KettleLAN – messages between the window (KettleLAN) and the Steam process (kettlelan-steam).
//
// Steam shows "Spacewar" for as long as the process that connected to it runs, so everything that
// touches Steam lives in a separate process that the window starts while a network is on and stops
// when all are off. They talk over the process's standard input and output: one JSON object per line.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <optional>

#include "engine.h"

namespace kettle::gui::ipc {

// --- Steam process → window
QByteArray encodeSnapshot(const Snapshot& snap);
QByteArray encodeEvent(const Message& message);

struct Incoming {
    enum class Kind { Snapshot, Event } kind;
    Snapshot snapshot;
    Message event;
};
std::optional<Incoming> decodeIncoming(const QByteArray& line);

// --- window → Steam process
QByteArray encodeCommand(const QJsonObject& command); // {"cmd": "join", "code": …, "name": …} etc.
std::optional<QJsonObject> decodeCommand(const QByteArray& line);

} // namespace kettle::gui::ipc
