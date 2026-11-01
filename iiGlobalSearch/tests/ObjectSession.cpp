#include "GlobalSearch.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace iiGlobalSearch;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main() try {
    ObjectSession session;
    FileObject first;
    first.id="files/portrait.png"; first.providerId="provider-42"; first.title="Café portrait";
    first.relativePath="files/portrait.png"; first.mimeType="image/png"; first.size=512;
    first.contentVersion="content-v1"; first.revision=7; first.modifiedAtNs=1234;
    first.metadata.description="Autumn campaign"; first.metadata.keywords={"approved", "portrait"};
    first.metadata.collections={"collection-1"}; first.metadata.rating=4; first.metadata.colorLabel="blue";
    first.metadata.fields={{"client","Aurora"}};
    first.metadata.additional=ii::core::Value(ii::core::Value::Object{{"camera",ii::core::Value("mirrorless")}});
    ObjectPresentation presentation; presentation.sourceVersion="version-123"; presentation.previewKind="image";
    presentation.properties={{"generator","Studio"}}; first.presentation=std::move(presentation);
    first.hasClassification=true;
    session.store(first);
    const auto saved=session.find(first.id);
    check(saved && saved->providerId=="provider-42" && saved->contentVersion=="content-v1" && saved->revision==7 &&
          saved->modifiedAtNs==1234 && saved->type==ObjectType::image &&
          saved->metadata.rating==4 && saved->metadata.additional.find("camera"), "stored identity, type or metadata lost");
    ObjectFilter filter;
    filter.text="CAFE portrait"; filter.types={ObjectType::image}; filter.minimumRating=4;
    filter.maximumRating=5; filter.allKeywords={"approved"}; filter.anyKeywords={"portrait", "missing"};
    filter.collections={"collection-1"}; filter.colorLabels={"blue"}; filter.fields={{"client","Aurora"}};
    filter.minimumSize=100; filter.maximumSize=1024; filter.mimeTypes={"image/png"};
    filter.minimumModifiedAtNs=1000; filter.maximumModifiedAtNs=2000;
    filter.extensions={".PNG"}; filter.pathPrefix="files"; filter.hasClassification=true; filter.hasPresentation=true;
    filter.contentVersion="content-v1"; filter.revision=7;
    filter.presentationFields={{"generator","Studio"}};
    const auto matches=session.search(filter,10);
    check(matches.size()==1 && matches.front().id==first.id, "combined provider metadata filters failed");
    filter.fields["client"]="Other";
    check(session.search(filter,10).empty(), "exact metadata field filter was ignored");
    filter.fields["client"]="Aurora"; filter.presentationFields["generator"]="Other";
    check(session.search(filter,10).empty(), "presentation property filter was ignored");
    ObjectFilter byIdentifier; byIdentifier.providerId="provider-42";
    check(session.search(byIdentifier,10).size()==1 && session.search(ObjectFilter{.text="mirrorless"},10).size()==1,
          "provider identifier or additional metadata is not searchable");
    check(session.search(ObjectFilter{.text="blue"},10).size()==1,
          "provider color label is not searchable");
    FileObject separatePath;
    separatePath.id="opaque-key-99"; separatePath.relativePath="deliverables/mountain/cover.png";
    session.store(separatePath);
    const auto byPathText=session.search(ObjectFilter{.text="mountain"},10);
    check(byPathText.size()==1 && byPathText.front().id==separatePath.id,
          "provider relative path is not searchable when it differs from the session id");
    bool rejected=false;
    try { session.store(first); } catch (const std::invalid_argument&) { rejected=true; }
    check(rejected, "duplicate session identity accepted");
    FileObject invalidText; invalidText.id=std::string("bad\0id",6);
    rejected=false; try { session.store(std::move(invalidText)); } catch (const std::invalid_argument&) { rejected=true; }
    check(rejected, "invalid session identity accepted");
    FileObject oversized; oversized.id="large"; oversized.metadata.description=std::string(65537,'x');
    rejected=false; try { session.store(std::move(oversized)); } catch (const std::invalid_argument&) { rejected=true; }
    check(rejected && !session.find("large"), "oversized metadata entered the session");

    // Matching after the first 1,024 indexed objects must still be returned.
    for (int i=0;i<1100;++i) {
        FileObject value; value.id="bulk/"+std::to_string(10000+i); value.title="common";
        if (i==1099) value.metadata.keywords={"last-match"};
        session.store(std::move(value));
    }
    ObjectFilter late; late.text="common"; late.allKeywords={"last-match"};
    const auto tail=session.search(late,1);
    check(tail.size()==1 && tail.front().id=="bulk/11099", "filter was applied after the candidate limit");
    ObjectFilter invalid; invalid.minimumRating=5; invalid.maximumRating=2;
    rejected=false; try { (void)session.search(invalid); } catch (const std::invalid_argument&) { rejected=true; }
    check(rejected, "inverted rating range accepted");
    invalid={}; invalid.text=std::string("bad\0query",9);
    rejected=false; try { (void)session.search(invalid); } catch (const std::invalid_argument&) { rejected=true; }
    check(rejected, "NUL search accepted");

    std::atomic<int> completed=0;
    std::thread worker([&] { try { (void)session.search(ObjectFilter{.text="common"},10); ++completed; }
                             catch (const std::logic_error&) {} });
    session.destroy();
    worker.join();
    check(session.destroyed(), "explicit destruction did not close session");
    session.destroy();
    rejected=false; try { (void)session.search(); } catch (const std::logic_error&) { rejected=true; }
    check(rejected, "destroyed session accepted a query");
    rejected=false; try { session.store(first); } catch (const std::logic_error&) { rejected=true; }
    check(rejected, "destroyed session accepted new objects");
    rejected=false; try { (void)session.find(first.id); } catch (const std::logic_error&) { rejected=true; }
    check(rejected, "destroyed session accepted a lookup");
    std::cout << "object session tests passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
