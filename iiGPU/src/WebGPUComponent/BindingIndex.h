#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ii::webgpu::detail {
// A request must fit within one lease; overlapping leases are not combined.
class BindingIndex final {
    struct Range final {
        std::uint64_t begin,end,best_end=0,best_whole_end=0;
        bool reaches_end=false,has_whole=false;
    };
    const void* single_buffer_=nullptr;
    std::optional<Range> single_;
    bool multiple_=false;
    std::unordered_map<const void*,std::vector<Range>> ranges_;
public:
    // Call only with an already validated slice inside its original buffer.
    void add(const void* buffer,std::uint64_t offset,std::uint64_t size,std::uint64_t total){
        if(!multiple_&&!single_){single_buffer_=buffer;single_.emplace(Range{offset,offset+size,0,0,offset+size==total,false});return;}
        if(!multiple_){ranges_[single_buffer_].push_back(*single_);single_.reset();multiple_=true;}
        ranges_[buffer].push_back({offset,offset+size,0,0,offset+size==total,false});
    }
    void finish(){
        if(!multiple_)return;
        for(auto& [buffer,ranges]:ranges_){
            (void)buffer;
            std::sort(ranges.begin(),ranges.end(),[](const Range& a,const Range& b){return a.begin<b.begin;});
            std::uint64_t best=0,whole_end=0;bool has_whole=false;
            for(auto& range:ranges){
                best=(std::max)(best,range.end);
                if(range.reaches_end){has_whole=true;whole_end=(std::max)(whole_end,range.end);}
                range.best_end=best;range.best_whole_end=whole_end;range.has_whole=has_whole;
            }
        }
    }
    [[nodiscard]] bool covers(const void* buffer,std::uint64_t offset,std::uint64_t size,bool whole)const{
        if(!multiple_){
            if(!single_||single_buffer_!=buffer||offset<single_->begin||offset>single_->end)return false;
            if(whole)return single_->reaches_end;
            return size<=single_->end-offset;
        }
        const auto found=ranges_.find(buffer);if(found==ranges_.end())return false;
        const auto& ranges=found->second;
        const auto next=std::upper_bound(ranges.begin(),ranges.end(),offset,[](std::uint64_t value,const Range& range){return value<range.begin;});
        if(next==ranges.begin())return false;
        const auto& prefix=*std::prev(next);
        if(whole)return prefix.has_whole&&prefix.best_whole_end>=offset;
        if(size>(std::numeric_limits<std::uint64_t>::max)()-offset)return false;
        return prefix.best_end>=offset+size;
    }
};
} // namespace ii::webgpu::detail
