#pragma once

#include "transport/transport.h"

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QList>
#include <QLowEnergyCharacteristic>
#include <QPointer>

class QLowEnergyController;
class QLowEnergyService;

// BLE ELM327 adapters have no standard profile: each brand puts a "serial"
// service somewhere (FFF0, FFE0, 18F0, ...) with one characteristic that
// notifies and one that accepts writes. This transport finds such a pair.
class BleTransport : public Transport
{
    Q_OBJECT

public:
    explicit BleTransport(const QBluetoothDeviceInfo &device, QObject *parent = nullptr);

    void open() override;
    void close() override;
    void write(const QByteArray &data) override;

private:
    void onServicesDiscovered();
    void tryNextService();
    void onServiceDetails(QLowEnergyService *service);

    QBluetoothDeviceInfo m_device;
    QPointer<QLowEnergyController> m_controller;
    QPointer<QLowEnergyService> m_service;
    QList<QBluetoothUuid> m_candidates;
    QLowEnergyCharacteristic m_notify;
    QLowEnergyCharacteristic m_write;
    bool m_ready = false;
};
