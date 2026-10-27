#include <iiSDKCore/Runtime/Runtime.h>
#include <iiJson/Json.h>
#include <iiVisualCodec/ImageCodec.h>
#include <iiXml/Xml.h>
#include <iiNetwork/WebSocket.h>
#include <iiFileProviderNative.h>
#include <iiFileProviderDatabase.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
using namespace iiFileProvider::native;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main(int argc,char** argv) {
    CHECK(argc==2);
    std::atomic_int executed{0}; ii::runtime::Runtime runtime({2,8});
    CHECK(runtime.post([&]{++executed;})); CHECK(runtime.join()); CHECK(executed==1);
    TemporaryDirectory temporary{std::filesystem::path(argv[1])}; CHECK(temporary.isValid());
    auto value=ii::json::parse("{\"kind\":\"preview\",\"width\":2}"); CHECK(value);
    auto json=ii::json::stringify(*value); CHECK(json);
    std::vector<std::uint8_t> pixels{11,12,13,255,14,15,16,0};
    auto png=ii::visual::image::encode_png({2,1,ii::visual::image::PixelFormat::rgba8,0,pixels}); CHECK(png);
    const std::string bytes(reinterpret_cast<const char*>(png->data()),png->size());
    File::write(temporary.path()/"preview.png",bytes);
    auto raw=File::read(temporary.path()/"preview.png"); CHECK(raw==bytes);
    auto image=ii::visual::image::decode({reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size()}); CHECK(image && image->rgba==pixels);
    sql::Options options; options.mode=sql::OpenMode::create;
    auto db=sql::Database::open(temporary.path()/"data.sqlite",options); CHECK(db);
    CHECK(db->execute("CREATE TABLE outputs (id INTEGER PRIMARY KEY, png BLOB, parameters TEXT)"));
    auto tx=sql::Transaction::begin(*db); CHECK(tx);
    auto insert=db->prepare("INSERT INTO outputs VALUES (1,?1,?2)"); CHECK(insert);
    CHECK(insert->bind(1,*png)); CHECK(insert->bind(2,*json)); CHECK(insert->step()==sql::Step::done); CHECK(tx->commit());
    auto query=db->prepare("SELECT png FROM outputs"); CHECK(query && query->step()==sql::Step::row);
    CHECK(std::get<sql::Blob>(*query->value(0))==*png);
    ii::xml::Writer writer; CHECK(writer.start("output")); CHECK(writer.text(*json)); CHECK(writer.end());
    auto xml=writer.finish(); CHECK(xml); File::write(temporary.path()/"manifest.xml",*xml);
    auto reader=ii::xml::Reader::open(File::read(temporary.path()/"manifest.xml")); CHECK(reader);
    unsigned events=0; while(true) { auto e=reader->next(); CHECK(e); if(!*e) break; ++events; } CHECK(events==3);
    ii::network::WebSocketClient request; request.host="localhost"; request.target="/outputs";
    auto client=ii::network::WebSocketSession::client(request),server=ii::network::WebSocketSession::server(); CHECK(client && server);
    CHECK(server->receive(client->take_output())); CHECK(server->upgrade()->target=="/outputs");
    CHECK(server->accept()); CHECK(client->receive(server->take_output())); CHECK(client->send_binary(bytes));
    auto delivered=server->receive(client->take_output()); CHECK(delivered && delivered->size()==1 && delivered->front().payload==bytes);
    std::cout << "Installed Qt-free JSON/PNG/XML/SQLite/WebSocket data path passed\n";
}
