#include "App/Account/AccountController.h"
#include "App/Account/DeviceInfo.h"
#include "App/Network/AutomaticPairing.h"
#include "App/Network/DiscoveryService.h"
#include "App/Network/NearbyDevices.h"
#include "App/Network/PairingCredentials.h"
#include "App/State/GroupSessionStore.h"
#include "App/State/StateCrypto.h"
#include <iiSocietyHelper.h>
#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    AccountController account(QUrl(QStringLiteral("http://127.0.0.1:1")));
    iiSocietyHelper::Client host(&account, nullptr);
    return host.container() && !host.connected() && !account.signedIn() ? 0 : 1;
}
