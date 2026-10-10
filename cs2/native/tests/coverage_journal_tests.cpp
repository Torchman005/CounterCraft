#include "coverage_journal.hpp"
#include <iostream>
#include <vector>

using namespace cc;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{
    CoverageJournal journal;
    std::vector<std::pair<uint64_t,bool>> captures;
    auto capture=[&](uint64_t resource,bool cleared){captures.emplace_back(resource,cleared);};
    // A foreground draw must survive an immediate clear or resource switch.
    journal.begin(11);journal.capture(11,capture);
    require(captures==decltype(captures){{11,false}},"First foreground draw was lost");
    journal.write(11);journal.clear(11,true,capture);
    require(captures.back()==std::pair<uint64_t,bool>{11,false},"Clear ran before retained capture");
    journal.write(11);journal.capture(11,capture);
    require(captures.back()==std::pair<uint64_t,bool>{11,true},"Post-clear write used world baseline");
    // Copy destinations can be first seen without any DSV binding, and may
    // remain unbound until effects. Flush must include ALL such destinations.
    journal.copy(12,capture);journal.copy(13,capture);journal.flush(capture);
    require(captures[captures.size()-2].first==12 && captures.back().first==13,"Unbound copy destination escaped final flush");
    journal.write(12);const auto prior=captures.size();journal.copy(12,capture);
    require(captures.size()==prior+1 && captures.back().first==12,"Copy erased earlier foreground writes");
    journal.clear(12,true,capture);require(journal.cleared(12),"Full clear baseline lost");
    journal.copy(12,capture);require(!journal.cleared(12),"Copy reused clear baseline");
    journal.write(11);journal.copy(11,capture);
    require(!journal.cleared(11),"World copy retained stale late-clear baseline");
    bool refused=false;
    try{journal.clear(13,false,capture);}catch(const std::exception&){refused=true;}
    require(refused,"Partial/unknown clear accepted");
    const auto before=captures.size();
    journal.reset();journal.flush(capture);journal.write(11);journal.copy(12,capture);
    require(captures.size()==before && !journal.contains(11),"History survived next interval");
    // An exceptional GPU capture must leave the write dirty for fail-closed
    // caller handling rather than report successful preservation.
    journal.begin(21);refused=false;
    try{journal.capture(21,[](uint64_t,bool){throw std::runtime_error("GPU failure");});}catch(const std::exception&){refused=true;}
    require(refused,"Capture failure was swallowed");journal.flush(capture);
    require(captures.back().first==21,"Failed capture cleared dirty history");
    journal.begin(31);
    for(size_t i=1;i<CoverageJournal::capacity;++i)journal.write(31+i);
    refused=false;try{journal.write(99);}catch(const std::exception&){refused=true;}
    require(refused,"Resource capacity overflow accepted");
    journal.begin(41);
    for(uint32_t i=0;i<CoverageJournal::capture_limit;++i){journal.write(41);journal.capture(41,capture);}
    refused=false;try{journal.capture(41,capture,true);}catch(const std::exception&){refused=true;}
    require(refused,"Capture budget overflow accepted");
    journal.begin(51);journal.capture(51,capture);const auto once=captures.size();journal.flush(capture);
    require(captures.size()==once,"Clean resources caused redundant copies");
    journal.capture(51,capture,true);require(captures.size()==once+1,"Final world depth not sampled");
    std::cout<<"Coverage event replay: draw/clear/copy/unbound flush/reset/failure/bounds passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
