#include "GenerationController.h"
#include "SocietyGenerationStorage.h"
#include <iiSocietyHelper.h>
#include <iiSocietyHelper/Sync/Sync.h>
#include <iiSocietyHelper/Generation/Host.h>
#include <StorageMap.h>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
[[noreturn]] void fail(const char *file, int line, const char *message)
{ throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + message); }
template<class A, class B> void compare(const A &actual, const B &expected, const char *file, int line, const char *message)
{ if (!(actual == expected)) fail(file, line, message); }
template<class Predicate> bool waitFor(Predicate predicate, int timeout)
{
    // Preserve the former event-pump budget: one slow I/O callback must not
    // consume all of the time allocated to subsequent event-loop turns.
    int waited = 0;
    while (!predicate() && waited < timeout) {
        const int step = std::min(50, timeout - waited);
        QEventLoop loop;
        QTimer::singleShot(step, &loop, &QEventLoop::quit);
        loop.exec();
        waited += step;
    }
    return predicate();
}
void processFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
#define CTEST_CHECK(e) do { if (!(e)) fail(__FILE__, __LINE__, #e); } while (false)
#define CTEST_CHECK_MESSAGE(e, m) do { if (!(e)) fail(__FILE__, __LINE__, (m)); } while (false)
#define CTEST_COMPARE(a, b) compare((a), (b), __FILE__, __LINE__, #a " == " #b)
#define CTEST_WAIT_VERIFY(e, t) CTEST_CHECK_MESSAGE(waitFor([&] { return bool(e); }, (t)), "timed out waiting for " #e)
#define CTEST_WAIT_MESSAGE(e, m, t) CTEST_CHECK_MESSAGE(waitFor([&] { return bool(e); }, (t)), (m))
#define CTEST_WAIT_DEFAULT(e) CTEST_WAIT_VERIFY(e, 5000)
#define CTEST_WAIT_MESSAGE_DEFAULT(e, m) CTEST_WAIT_MESSAGE(e, m, 5000)
#define CTEST_WAIT_COMPARE(a, b, t) CTEST_WAIT_VERIFY(((a) == (b)), (t))
#define CTEST_WAIT_COMPARE_DEFAULT(a, b) CTEST_WAIT_COMPARE(a, b, 5000)

QString workspaceTemplate()
{
    CTEST_CHECK(QDir().mkpath(QStringLiteral(SOCIETY_HELPER_TEST_RESOURCES)));
    return QStringLiteral(SOCIETY_HELPER_TEST_RESOURCES "/dreamscapes-XXXXXX");
}
class TestWorkspace {
public:
    TestWorkspace() : directory_(workspaceTemplate()), hadContainer_(qEnvironmentVariableIsSet("SOCIETY_CONTAINER_PATH")),
                      previousContainer_(qgetenv("SOCIETY_CONTAINER_PATH"))
    {
        CTEST_CHECK(directory_.isValid());
        std::cerr << "Dreamscapes test workspace: " << directory_.path().toStdString() << '\n';
        CTEST_CHECK(qputenv("SOCIETY_CONTAINER_PATH", QFile::encodeName(filePath("unselected.societycontainer"))));
    }
    ~TestWorkspace()
    {
        if (hadContainer_) qputenv("SOCIETY_CONTAINER_PATH", previousContainer_);
        else qunsetenv("SOCIETY_CONTAINER_PATH");
    }
    QString filePath(const QString &name) const { return directory_.filePath(name); }
private:
    QTemporaryDir directory_;
    bool hadContainer_;
    QByteArray previousContainer_;
};
class Runner {
public:
    Runner(int argc, char **argv) : selectAll_(argc == 1)
    { for (int i = 1; i < argc; ++i) remaining_.insert(argv[i]); }
    template<class Function> void run(const char *name, Function function)
    {
        if (!selectAll_ && !remaining_.erase(name)) return;
        ++cases_;
        try { TestWorkspace workspace; function(workspace); }
        catch (const std::exception &error) { ++failures_; std::cerr << name << ": " << error.what() << '\n'; }
        catch (...) { ++failures_; std::cerr << name << ": unexpected exception\n"; }
    }
    int result() const
    {
        if (!remaining_.empty() || cases_ == 0) { std::cerr << "Unknown or empty test selection\n"; return 1; }
        std::cout << cases_ << " test cases, " << failures_ << " failures\n";
        return failures_ ? 1 : 0;
    }
private:
    bool selectAll_;
    std::set<std::string> remaining_;
    int cases_ = 0;
    int failures_ = 0;
};
class BackgroundActivity final : public GenerationBackgroundActivity {
public:
    bool permitted = true;
    bool cpuOnly = false;
    int starts = 0;
    int networkStarts = 0;
    QList<bool> completions;
    QList<QPair<QString, QString>> presentations;
    bool presentationPaused = false;
    int updates = 0;
    iiLocalDiffusion::NativeGenerationStage lastStage = iiLocalDiffusion::NativeGenerationStage::Waiting;
    std::function<void(bool)> foregroundChanged;
    std::function<void()> expiration;
    void observeForeground(std::function<void(bool)> changed) override { foregroundChanged = std::move(changed); }
    void begin(const QString &, std::function<void()> expired) override { ++starts; expiration = std::move(expired); }
    void beginNetwork(const QString &job, std::function<void()> expired) override { ++networkStarts; begin(job, std::move(expired)); }
    bool allowsBackgroundExecution() const override { return permitted && starts > completions.size(); }
    bool requiresCpuExecution() const override { return cpuOnly; }
    void update(const iiLocalDiffusion::NativeGenerationProgress &progress) override { ++updates; lastStage = progress.stage; }
    void end(bool success) override { completions.append(success); }
    void finishPresentation(const QString &job, const QString &state) override { presentations.append({job, state}); }
    void setPresentationPaused(bool paused) override { presentationPaused = paused; }
    QVariantMap status() const override { return {}; }
};

iiLocalDiffusion::NativeGenerationResult imageResult(const iiLocalDiffusion::NativeGenerationRequest &request)
{
    return {std::vector<std::uint8_t>(request.width * request.height * 3, 127), request.width, request.height};
}
void nativePreviewsPublishRealPixelsAcrossPassesAndCleanUp(const TestWorkspace &workspace) {
        QTemporaryDir storage(workspace.filePath("native-preview-XXXXXX.societycontainer"));
        CTEST_CHECK(storage.isValid());
        CTEST_CHECK(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        CTEST_CHECK(model.open(QIODevice::WriteOnly)); model.write("test model"); model.close();
        GenerationRuntime runtime;
        runtime.modelPreferencesFile = workspace.filePath("model-preferences.json");
        runtime.nativeInference = true; runtime.imageExtent = 64;
        auto engineReturned = std::make_shared<std::atomic_bool>(false);
        runtime.nativeGenerate = [engineReturned](const auto &request, const auto &, const auto &, const auto &progress, const auto &preview) {
            using Stage = iiLocalDiffusion::NativeGenerationStage;
            for (int sequence = 1; sequence <= 2; ++sequence) {
                const auto step = sequence == 1 ? 10 : 1, total = sequence == 1 ? 10 : 3;
                // A progress event may arrive before the pixels for the same step.
                progress({Stage::Denoising, step, total});
                preview({std::vector<std::uint8_t>(2 * 2 * 3, sequence * 40), 2, 2, sequence, step, total});
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            auto result = imageResult(request);
            *engineReturned = true;
            return result;
        };
        GenerationController app(runtime);
        CTEST_CHECK(app.connectStorage(storage.path())); app.setForeground(true);
        QList<QUrl> frames; QList<int> totals; QList<int> previewPixels; QList<bool> previewWasUnpublished;
        QObject previewObserver;
        const auto previewConnection = QObject::connect(&app, &GenerationController::previewChanged, &previewObserver, [&] {
            if (app.previewImage().isEmpty() || frames.contains(app.previewImage())) return;
            frames.append(app.previewImage()); totals.append(app.previewTotalSteps());
            previewPixels.append(QImage(app.previewImage().toLocalFile()).pixelColor(0, 0).red());
            previewWasUnpublished.append(app.latestImage().isEmpty());
        });
        CTEST_CHECK(previewConnection);
        CTEST_CHECK(!app.enqueue("show the image as it develops").isEmpty());
        CTEST_WAIT_MESSAGE(!app.latestImage().isEmpty(),
            qPrintable(QStringLiteral("image publication timed out; error=%1; jobs=%2; preview frames=%3; engine returned=%4; inference=%5")
                .arg(app.errorString(), QString::fromUtf8(QJsonDocument::fromVariant(app.jobs()).toJson(QJsonDocument::Compact)))
                .arg(frames.size()).arg(engineReturned->load())
                .arg(QString::fromUtf8(QJsonDocument::fromVariant(app.inferenceStatus()).toJson(QJsonDocument::Compact)))), 5000);
        CTEST_COMPARE(frames.size(), 2); CTEST_COMPARE(totals, QList<int>({10, 3}));
        CTEST_COMPARE(previewPixels, QList<int>({40, 80}));
        CTEST_COMPARE(previewWasUnpublished, QList<bool>({true, true}));
        CTEST_CHECK(app.previewImage().isEmpty());
        for (const auto &frame : frames) CTEST_CHECK(!QFileInfo::exists(frame.toLocalFile()));
        CTEST_COMPARE(QImage(app.latestImage().toLocalFile()).size(), QSize(64, 64));
    }
void initialMirrorStaysUnavailableUntilSocietyPublishesIt(const TestWorkspace &workspace) {
        QTemporaryDir phone(workspace.filePath("society-pending-XXXXXX.societycontainer"));
        CTEST_CHECK(phone.isValid());
        const auto local = iiSocietyContainer::SocietyDrive::create(phone.path());
        CTEST_CHECK(local);
        const auto hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        CTEST_CHECK(iiSocietyContainer::SocietyDrive::adoptReplicaIdentity(phone.path(), local->identifier(), hostId));
        QFile model(phone.filePath("Models/pending.safetensors"));
        CTEST_CHECK(model.open(QIODevice::WriteOnly)); model.write("model"); model.close();
        GenerationRuntime runtime;
        runtime.modelPreferencesFile = workspace.filePath("model-preferences.json");
        GenerationController app(runtime);
        CTEST_CHECK(!app.connectStorage(phone.path()));
        app.setForeground(true);
        CTEST_CHECK(app.models().isEmpty());
        CTEST_CHECK(app.enqueue("not ready").isEmpty());
        CTEST_CHECK(iiSocietyContainer::SocietyDrive::completeReplica(phone.path(), hostId));
        CTEST_WAIT_VERIFY(app.connected(), 6000);
        CTEST_COMPARE(app.models().size(), 1);
        CTEST_CHECK(app.errorString().isEmpty());
        CTEST_CHECK(QFile::remove(model.fileName()));
        CTEST_WAIT_VERIFY(app.models().isEmpty(), 6000);
        CTEST_CHECK(app.enqueue("removed model").isEmpty());
    }
void generatesOnHostBeforeBackgroundModelDownloadThenUsesLocalCache(const TestWorkspace &workspace) {
        QTemporaryDir desktop(workspace.filePath("society-host-XXXXXX.societycontainer"));
        CTEST_CHECK(desktop.isValid());
        QTemporaryDir phone(workspace.filePath("society-phone-XXXXXX.societycontainer"));
        CTEST_CHECK(phone.isValid());
        CTEST_CHECK(iiSocietyContainer::SocietyDrive::create(desktop.path()));
        CTEST_CHECK(iiSocietyContainer::SocietyDrive::create(phone.path()));
        QFile weights(desktop.filePath("Models/synced.safetensors"));
        const QByteArray modelBytes(700000, 'm');
        CTEST_CHECK(weights.open(QIODevice::WriteOnly)); CTEST_COMPARE(weights.write(modelBytes), modelBytes.size()); weights.close();
        const auto resources = QStringLiteral("Models/.generation-resources/iiLocalDiffusion/");
        const auto runtimeCache = QStringLiteral("Models/.society-runtime/iiLocalDiffusion/q8/");
        for (const auto &relative : {resources + "generation-defaults.json", resources + "vae.safetensors", runtimeCache + "model.gguf"}) {
            CTEST_CHECK(QDir().mkpath(QFileInfo(desktop.filePath(relative)).absolutePath()));
            QFile fixture(desktop.filePath(relative));
            CTEST_CHECK(fixture.open(QIODevice::WriteOnly)); fixture.write("fixture");
        }

        iiWebServer::LanPeer host, client;
        auto remoteGeneration = std::make_shared<iiSocietyHelper::Generation::Remote>([&](const QJsonObject &request) {
            return client.request("desktop", request);
        });
        QObject::connect(&client, &iiWebServer::LanPeer::completed, remoteGeneration.get(),
            [remoteGeneration](auto id, auto result, auto) { remoteGeneration->receive(id, result); });
        std::atomic_int hostRuns{0};
        std::string hostModelPath;
        iiSocietyHelper::Generation::Host generationHost([&](const auto &request, const auto &, const auto &, const auto &progress) {
            progress({iiLocalDiffusion::NativeGenerationStage::Loading, 25, 100});
            hostModelPath = request.modelPath.string(); ++hostRuns; return imageResult(request);
        });
        CTEST_CHECK(generationHost.open(desktop.path()));
        GenerationRuntime runtime{DREAMSCAPES_FAKE_GENERATOR, "cpu", 1, 64, {}};
        runtime.modelPreferencesFile = workspace.filePath("model-preferences.json");
        runtime.remoteGeneration = remoteGeneration;
        runtime.nativeInference = true;
        runtime.nativeGenerate = [](const auto &request, const auto &, const auto &, const auto &, const auto &) {
            return imageResult(request);
        };
        auto background = std::make_shared<BackgroundActivity>();
        runtime.backgroundActivity = background;
        GenerationController app(runtime);
        CTEST_CHECK(app.connectStorage(phone.path()));
        CTEST_CHECK(app.models().isEmpty());
        app.setForeground(true);

        // Only the two Society roles own transports and synchronization.
        iiSocietyHelper::Sync::Controller hosting({}), syncing([&](const auto &peer, const auto &request) {
            return client.request(peer, request);
        });
        const auto files = iiSocietyHelper::Sync::filesHandler(desktop.path());
        CTEST_CHECK(host.startHost("desktop", "Desktop Society", [&](const auto &peer, const auto &request) {
            if (request.value("op") == "society.generation") return generationHost.handle(peer, request);
            return request.value("op") == "society.sync" ? hosting.handle(peer, request) : files(peer, request);
        }, {"127.0.0.1"}, iiWebServer::Address::loopback()));
        CTEST_CHECK(client.join(host.createOffer(), "phone", "iPhone Society"));
        CTEST_WAIT_MESSAGE_DEFAULT(client.connected(), qPrintable(client.errorString()));
        const QString scope(64, 'a');
        hosting.open(desktop.path(), scope); syncing.open(phone.path(), scope);
        // Initial replica creation opens SQLite and validates both container
        // roots on workers. This setup budget is separate from job deadlines.
        CTEST_WAIT_MESSAGE(hosting.available() && syncing.available(),
            qPrintable(QStringLiteral("Host: %1; client: %2")
                .arg(hosting.errorString(), syncing.errorString())), 15000);
        hosting.setPeers({"phone"}, {});
        QObject::connect(&client, &iiWebServer::LanPeer::completed, &syncing, [&](auto id, auto result, auto) {
            syncing.receive(id, result);
        });
        int synchronizedCount = 0;
        QObject syncObserver;
        const auto synchronizedConnection = QObject::connect(&syncing, &iiSocietyHelper::Sync::Controller::synchronized,
            &syncObserver, [&] { ++synchronizedCount; });
        CTEST_CHECK(synchronizedConnection);
        syncing.setPeers({"desktop"}, {"desktop"});
        CTEST_WAIT_MESSAGE(synchronizedCount > 0, qPrintable(syncing.errorString()), 30000);
        CTEST_WAIT_COMPARE(app.models().size(), 1, 6000);
        CTEST_CHECK(app.connected());
        CTEST_COMPARE(app.containerPath(), phone.path());
        iiSocietyHelper::FileSystem local;
        CTEST_CHECK(local.open(phone.path()));
        CTEST_COMPARE(local.containerId(), iiSocietyContainer::SocietyDrive::open(desktop.path())->identifier());
        CTEST_COMPARE(local.path("models", "synced.safetensors"), phone.filePath("Models/synced.safetensors"));
        CTEST_CHECK(!QFileInfo::exists(local.path("models", "synced.safetensors")));
        CTEST_CHECK(!app.models().first().toMap().value("available").toBool());
        // Hold model replication while leaving the authenticated transport live.
        // The result must arrive even when no model byte can be downloaded.
        syncing.setPeers({}, {});
        const auto id = app.enqueue("host first generation", "1:1", 2);
        CTEST_CHECK_MESSAGE(!id.isEmpty(), qPrintable(app.errorString()));
        CTEST_WAIT_COMPARE_DEFAULT(background->starts, 1);
        background->permitted = false;
        app.setForeground(false);
        processFor(300);
        background->permitted = true;
        // The OS may grant execution after UIKit has already entered background.
        app.setForeground(false);
        CTEST_WAIT_COMPARE(app.completedResults().size(), 2, 30000);
        CTEST_COMPARE(hostRuns.load(), 2);
        CTEST_COMPARE(background->lastStage, iiLocalDiffusion::NativeGenerationStage::Loading);
        CTEST_COMPARE(QString::fromStdString(hostModelPath), desktop.filePath("Models/synced.safetensors"));
        CTEST_CHECK(!QFileInfo::exists(local.path("models", "synced.safetensors")));
        CTEST_COMPARE(app.latestResult().value("execution").toString(), QString("host"));
        const auto download = app.latestResult().value("modelDownload").toMap().value("requestId").toString();
        CTEST_CHECK(!download.isEmpty());
        iiSocietyContainer::StorageMap map(*iiSocietyContainer::SocietyDrive::open(phone.path()));
        CTEST_CHECK(map.requestState(download).value("state") != "ready");
        CTEST_COMPARE(background->starts, 1); CTEST_COMPARE(background->networkStarts, 1); CTEST_CHECK(background->completions.isEmpty());
        app.setForeground(false);
        CTEST_CHECK(background->allowsBackgroundExecution());
        background->expiration();
        CTEST_COMPARE(background->completions, QList<bool>{false});
        CTEST_CHECK(map.requestState(download).value("state") != "cancelled");
        app.setForeground(true);
        syncing.setPeers({"desktop"}, {"desktop"}); syncing.synchronizeNow();
        CTEST_WAIT_COMPARE(map.requestState(download).value("state").toString(), QString("ready"), 30000);
        QFile copied(local.path("models", "synced.safetensors"));
        CTEST_CHECK(copied.open(QIODevice::ReadOnly)); CTEST_COMPARE(copied.readAll(), modelBytes); copied.close();
        for (const auto &relative : {resources + "generation-defaults.json", resources + "vae.safetensors"}) {
            QFile resource(phone.filePath(relative));
            CTEST_CHECK(resource.open(QIODevice::ReadOnly)); CTEST_COMPARE(resource.readAll(), "fixture");
        }
        CTEST_CHECK(!QFileInfo::exists(phone.filePath(runtimeCache + "model.gguf")));
        const auto result = app.latestResult();
        CTEST_COMPARE(result.value("generation").toMap().value("backend").toString(), QString("society-host"));
        const auto image = result.value("imageSource").toUrl().toLocalFile();
        CTEST_CHECK(image.startsWith(phone.filePath("Generation History/")));
        CTEST_CHECK(image.endsWith(".png"));
        const auto remote = desktop.filePath("Generation History/" + QFileInfo(image).fileName());
        CTEST_WAIT_VERIFY(QFileInfo::exists(remote), 15000);
        QFile original(image), published(remote); CTEST_CHECK(original.open(QIODevice::ReadOnly)); CTEST_CHECK(published.open(QIODevice::ReadOnly));
        CTEST_COMPARE(published.readAll(), original.readAll());
        syncing.closeAndWait(); hosting.closeAndWait(); client.stop(); host.stop();
        // The downloaded cache is usable without any host connection.
        const auto completedBeforeOffline = app.completedResults().size();
        CTEST_CHECK(!app.enqueue("offline cached generation").isEmpty());
        CTEST_WAIT_COMPARE(app.completedResults().size(), completedBeforeOffline + 1, 10000);
        CTEST_COMPARE(hostRuns.load(), 2);

    }
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Runner runner(argc, argv);
    runner.run("initialMirrorStaysUnavailableUntilSocietyPublishesIt", initialMirrorStaysUnavailableUntilSocietyPublishesIt);
    runner.run("nativePreviewsPublishRealPixelsAcrossPassesAndCleanUp", nativePreviewsPublishRealPixelsAcrossPassesAndCleanUp);
    runner.run("generatesOnHostBeforeBackgroundModelDownloadThenUsesLocalCache", generatesOnHostBeforeBackgroundModelDownloadThenUsesLocalCache);
    return runner.result();
}
