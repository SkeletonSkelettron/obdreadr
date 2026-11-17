#pragma once

#include "obd/simecu.h"
#include "transport/transport.h"

#include <QElapsedTimer>

// Demo mode: talks to SimEcu with a delay similar to a real Bluetooth adapter.
class SimTransport : public Transport
{
    Q_OBJECT

public:
    explicit SimTransport(QObject *parent = nullptr);

    void open() override;
    void close() override;
    void write(const QByteArray &data) override;

private:
    SimEcu m_ecu;
    QElapsedTimer m_clock;
    QByteArray m_input;
    bool m_open = false;
};
