#include "Society/Account/AccountController.h"
#include "Society/Account/DeviceInfo.h"
#include "Society/Network/AutomaticPairing.h"
#include "Society/Network/DiscoveryService.h"
#include "Society/Network/NearbyDevices.h"
#include "Society/Network/PairingCredentials.h"
#include "Society/State/GroupSessionStore.h"
#include "Society/State/StateCrypto.h"
#include <iiSocietyHelper.h>
#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    AccountController account(QUrl(QStringLiteral("http://127.0.0.1:1")));
    iiSocietyHelper::Client host(&account, nullptr);
    return host.container() && !host.connected() && !account.signedIn() ? 0 : 1;
}
