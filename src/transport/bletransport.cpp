#include "transport/bletransport.h"

#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>

namespace {

// Serial services used by common BLE OBD adapters, most likely first.
const QList<QBluetoothUuid> &knownServices()
{
    static const QList<QBluetoothUuid> services = {
        QBluetoothUuid(quint16(0xFFF0)),
        QBluetoothUuid(quint16(0xFFE0)),
        QBluetoothUuid(quint16(0x18F0)),
        QBluetoothUuid(QUuid::fromString(u"{E7810A71-73AE-499D-8C15-FAA9AEF0C3F2}")),
    };
    return services;
}

bool isStandardService(const QBluetoothUuid &uuid)
{
    bool ok = false;
    const quint16 id = uuid.toUInt16(&ok);
    // Generic Access, Generic Attribute, Device Information, Battery.
    return ok && (id == 0x1800 || id == 0x1801 || id == 0x180A || id == 0x180F);
}

constexpr int kMaxChunk = 20; // payload of the default 23 byte ATT MTU

} // namespace

BleTransport::BleTransport(const QBluetoothDeviceInfo &device, QObject *parent)
    : Transport(parent)
    , m_device(device)
{
}

void BleTransport::open()
{
    m_controller = QLowEnergyController::createCentral(m_device, this);
    connect(m_controller, &QLowEnergyController::connected, this, [this] {
        m_controller->discoverServices();
    });
    connect(m_controller, &QLowEnergyController::discoveryFinished, this, &BleTransport::onServicesDiscovered);
    connect(m_controller, &QLowEnergyController::disconnected, this, &Transport::closed);
    connect(m_controller, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error) {
        emit errorOccurred(m_controller ? m_controller->errorString() : tr("Bluetooth LE error"));
    });
    m_controller->connectToDevice();
}

void BleTransport::close()
{
    m_ready = false;
    if (m_controller) {
        m_controller->disconnect(this);
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
    }
}

void BleTransport::write(const QByteArray &data)
{
    if (!m_ready || !m_service)
        return;
    const auto mode = (m_write.properties() & QLowEnergyCharacteristic::WriteNoResponse)
                          ? QLowEnergyService::WriteWithoutResponse
                          : QLowEnergyService::WriteWithResponse;
    for (int offset = 0; offset < data.size(); offset += kMaxChunk)
        m_service->writeCharacteristic(m_write, data.mid(offset, kMaxChunk), mode);
}

void BleTransport::onServicesDiscovered()
{
    const QList<QBluetoothUuid> found = m_controller->services();
    m_candidates.clear();
    for (const QBluetoothUuid &uuid : knownServices()) {
        if (found.contains(uuid))
            m_candidates << uuid;
    }
    for (const QBluetoothUuid &uuid : found) {
        if (!m_candidates.contains(uuid) && !isStandardService(uuid))
            m_candidates << uuid;
    }
    tryNextService();
}

void BleTransport::tryNextService()
{
    if (m_candidates.isEmpty()) {
        emit errorOccurred(tr("No serial (ELM327) service found on this BLE device"));
        return;
    }
    const QBluetoothUuid uuid = m_candidates.takeFirst();
    QLowEnergyService *service = m_controller->createServiceObject(uuid, this);
    if (!service) {
        tryNextService();
        return;
    }
    connect(service, &QLowEnergyService::stateChanged, this, [this, service](QLowEnergyService::ServiceState state) {
        if (state == QLowEnergyService::RemoteServiceDiscovered)
            onServiceDetails(service);
    });
    service->discoverDetails();
}

void BleTransport::onServiceDetails(QLowEnergyService *service)
{
    QLowEnergyCharacteristic notify;
    QLowEnergyCharacteristic writable;
    const QList<QLowEnergyCharacteristic> characteristics = service->characteristics();
    for (const QLowEnergyCharacteristic &c : characteristics) {
        const auto props = c.properties();
        if (!notify.isValid() && (props & (QLowEnergyCharacteristic::Notify | QLowEnergyCharacteristic::Indicate)))
            notify = c;
        if (!writable.isValid() && (props & (QLowEnergyCharacteristic::Write | QLowEnergyCharacteristic::WriteNoResponse)))
            writable = c;
    }
    if (!notify.isValid() || !writable.isValid()) {
        service->deleteLater();
        tryNextService();
        return;
    }

    m_service = service;
    m_notify = notify;
    m_write = writable;
    connect(service, &QLowEnergyService::characteristicChanged, this,
            [this](const QLowEnergyCharacteristic &c, const QByteArray &value) {
                if (c.uuid() == m_notify.uuid())
                    emit dataReceived(value);
            });
    connect(service, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError error) {
        if (error != QLowEnergyService::NoError)
            emit errorOccurred(tr("Bluetooth LE service error %1").arg(int(error)));
    });

    const QLowEnergyDescriptor cccd =
        notify.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (cccd.isValid()) {
        const bool useNotify = notify.properties() & QLowEnergyCharacteristic::Notify;
        service->writeDescriptor(cccd, QByteArray::fromHex(useNotify ? "0100" : "0200"));
    }
    m_ready = true;
    emit opened();
}
