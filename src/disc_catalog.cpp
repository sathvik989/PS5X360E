// SPDX-License-Identifier: MIT
#include "disc_catalog.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace ps5x360_disc {
namespace {
namespace fs=std::filesystem;
fs::path NativePath(const std::string&s){
#if defined(__cpp_char8_t)
 return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()),s.size()));
#else
 return fs::u8path(s);
#endif
}
std::string PathText(const fs::path&p){auto u=p.generic_u8string();return std::string(reinterpret_cast<const char*>(u.data()),u.size());}
struct Failure:std::runtime_error {Error code;Failure(Error c,const std::string&s):runtime_error(s),code(c){}};
void Need(bool b,Error e,const char*s){if(!b)throw Failure(e,s);}
std::string Lower(std::string s){for(auto&c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
std::string Trim(std::string s){auto a=s.find_first_not_of(" \t\r");if(a==s.npos)return {};auto b=s.find_last_not_of(" \t\r");return s.substr(a,b-a+1);}
bool Utf8(const std::string&s){for(size_t i=0;i<s.size();){unsigned char c=s[i++];if(c<128)continue;unsigned n=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:9;if(n==9||i+n>s.size())return false;unsigned char first=s[i];if((c==0xe0&&first<0xa0)||(c==0xed&&first>=0xa0)||(c==0xf0&&first<0x90)||(c==0xf4&&first>=0x90))return false;while(n--)if((static_cast<unsigned char>(s[i++])&0xc0)!=0x80)return false;}return true;}
bool Drive(const std::string&s){return s.size()>=3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1]==':' && s[2]=='/';}
std::string Lexical(std::string s){Need(!s.empty()&&s.size()<=4096&&Utf8(s),Error::InvalidPath,"Empty, oversized or non-UTF8 path");std::replace(s.begin(),s.end(),'\\','/');
 Need(s.compare(0,2,"//")!=0,Error::InvalidPath,"UNC/network paths are unsupported");bool drive=Drive(s);Need(s[0]=='/' || drive,Error::InvalidPath,"Absolute path required");
 for(size_t i=0;i<s.size();++i){auto c=static_cast<unsigned char>(s[i]);Need(c>=32&&c!=127&&c!='"'&&c!='<'&&c!='>'&&c!='|'&&c!='?'&&c!='*'&&(c!=':'||(drive&&i==1)),Error::InvalidPath,"Unsafe path syntax");}
 std::string root=drive?s.substr(0,3):"/";if(drive)root[0]=char(std::toupper(static_cast<unsigned char>(root[0])));std::vector<std::string> parts;size_t at=root.size();
 while(at<s.size()){size_t end=s.find('/',at);if(end==s.npos)end=s.size();auto p=s.substr(at,end-at);if(p==".."){Need(!parts.empty(),Error::InvalidPath,"Path traverses filesystem root");parts.pop_back();}else if(!p.empty()&&p!=".")parts.push_back(p);at=end+1;}
 std::string out=root;for(const auto&p:parts){if(out.back()!='/')out+='/';out+=p;}return out;
}
std::string Parent(const std::string&s){auto p=s.find_last_of('/');Need(p!=s.npos,Error::InvalidPath,"Path parent unavailable");if(p==0)return "/";if(p==2&&Drive(s))return s.substr(0,3);return s.substr(0,p);}
std::string Name(const std::string&s){return s.substr(s.find_last_of('/')+1);}
std::string Extension(const std::string&s){auto n=Name(s);auto at=n.find_last_of('.');return at==n.npos?std::string():Lower(n.substr(at));}
std::string Key(const std::string&s){return Drive(s)?Lower(s):s;}
bool Within(const std::string&p,const std::string&r){auto path=Key(p),root=Key(r);return path==root || (path.size()>root.size() && path.compare(0,root.size(),root)==0 && (root.back()=='/'||path[root.size()]=='/'));}
void Allowed(const std::string&p,const std::vector<std::string>&roots){Need(!roots.empty(),Error::InvalidPath,"No media root configured");for(const auto&r:roots)if(Within(p,Lexical(r)))return;throw Failure(Error::InvalidPath,"Media path escapes allowed roots");}
std::string Canonical(const std::string&p,const std::vector<std::string>&roots){std::error_code ec;auto actual=fs::canonical(NativePath(p),ec);Need(!ec,Error::MissingMedia,"Media path missing/unreadable");auto normalized=Lexical(PathText(actual));Allowed(normalized,roots);return normalized;}
std::string Text(const std::string&p,size_t limit){std::ifstream f(NativePath(p),std::ios::binary);Need(bool(f),Error::MissingMedia,"Cannot read playlist");std::string s;std::array<char,4096>b;while(f){f.read(b.data(),b.size());auto n=f.gcount();Need(s.size()+size_t(n)<=limit,Error::MalformedPlaylist,"Playlist exceeds 64 KiB");s.append(b.data(),size_t(n));}Need(f.eof(),Error::MissingMedia,"Playlist read failed");return s;}
std::vector<fs::directory_entry> Entries(const std::string&dir,size_t limit){std::error_code ec;fs::directory_iterator it(NativePath(dir),ec),end;Need(!ec,Error::MissingMedia,"Cannot enumerate media directory");std::vector<fs::directory_entry> v;while(it!=end){Need(v.size()<limit,Error::TooManyMembers,"Media directory entry limit exceeded");v.push_back(*it);it.increment(ec);Need(!ec,Error::MissingMedia,"Media enumeration failed");}std::sort(v.begin(),v.end(),[](const auto&a,const auto&b){return PathText(a.path())<PathText(b.path());});return v;}
std::string MountInput(const std::string&path,const std::vector<std::string>&roots,const Options&o){auto p=Canonical(path,roots);std::error_code ec;bool directory=fs::is_directory(NativePath(p),ec);Need(!ec,Error::MissingMedia,"Cannot inspect media");if(directory){std::vector<std::string> matches;for(auto&e:Entries(p,o.max_directory_entries))if(Lower(PathText(e.path().filename()))=="default.xex")matches.push_back(PathText(e.path()));Need(matches.size()==1, matches.empty()?Error::MissingMedia:Error::AmbiguousDisc,"Directory needs one unambiguous default.xex");p=Canonical(matches[0],roots);}
 Need(fs::is_regular_file(NativePath(p),ec)&&!ec,Error::MissingMedia,"Media must be a regular file");auto ext=Extension(p);Need(ext==".xex"||ext==".iso"||ext==".zar",Error::UnsupportedMedia,"Supported mount inputs: XEX, ISO, ZAR or extracted directory");return p;
}
struct Hint {unsigned number=0;std::string family;};
bool Sep(char c){return c==' '||c=='_'||c=='-'||c=='.';}
// Terminal Disc N / Disk N token only. The remaining prefix is a physical filename family.
Hint DiscHint(std::string n){auto ext=Extension(n);if(ext==".iso"||ext==".zar")n.resize(n.size()-ext.size());n=Lower(Trim(n));if(!n.empty()&&(n.back()==')'||n.back()==']'))n.pop_back();n=Trim(n);
 size_t end=n.size(),start=end;while(start && std::isdigit(static_cast<unsigned char>(n[start-1])))--start;if(start==end)return {};
 auto digits=n.substr(start);if(digits.size()>3 || digits[0]=='0')return {};unsigned number=unsigned(std::stoul(digits));if(!number||number>255)return {};
 size_t word_end=start;while(word_end && Sep(n[word_end-1]))--word_end;if(word_end<4)return {};size_t length=word_end>=5 && n.substr(word_end-5,5)=="disco"?5:4;auto word=n.substr(word_end-length,length);if(word!="disc" && word!="disk" && word!="disco")return {};size_t word_start=word_end-length;
 if(word_start && !Sep(n[word_start-1]) && n[word_start-1]!='(' && n[word_start-1]!='[')return {};
 std::string family=n.substr(0,word_start);while(!family.empty()&&(Sep(family.back())||family.back()=='('||family.back()=='['))family.pop_back();return {number,family};
}
uint32_t BE(const uint8_t*p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
bool Valid(const ExecutionInfo&i){return i.title_id!=0 && i.disc_number>=1 && i.disc_count>=1 && i.disc_number<=i.disc_count;}
bool Family(const ExecutionInfo&a,const ExecutionInfo&b){return a.title_id==b.title_id && a.disc_count==b.disc_count && a.version==b.version && a.base_version==b.base_version && a.savegame_id==b.savegame_id && a.platform==b.platform && a.executable_table==b.executable_table;}
bool Same(const ExecutionInfo&a,const ExecutionInfo&b){return Family(a,b)&&a.disc_number==b.disc_number&&a.media_id==b.media_id;}
MetadataResult ProbeInput(const std::string&p,const Probe&probe){return Extension(p)==".xex"?ReadXexExecutionInfo(p):probe?probe(p):MetadataResult{{},"Image metadata requires the read-only VFS probe callback"};}
}

unsigned DiscNumberFromName(const std::string& name) { return DiscHint(name).number; }

MetadataResult ParseXexExecutionInfo(const uint8_t*b,size_t bytes){auto bad=[](const char*s){return MetadataResult{{},s};};if(!b||bytes<24||memcmp(b,"XEX2",4))return bad("Not a complete XEX2 fixed header");uint32_t header=BE(b+8),count=BE(b+20);if(header<24||header>2u<<20||count>4096||uint64_t(24)+uint64_t(count)*8>header||bytes<header)return bad("XEX optional-header area truncated/out of bounds");
 uint32_t offset=0;unsigned matches=0;for(uint32_t i=0;i<count;++i)if(BE(b+24+i*8)==0x00040006){++matches;offset=BE(b+28+i*8);}
 if(matches!=1)return bad(matches?"Duplicate XEX execution-info header":"XEX execution-info header missing");if(offset<24+count*8 || uint64_t(offset)+24>header)return bad("Execution-info offset out of bounds");const auto*p=b+offset;
 ExecutionInfo info{BE(p),BE(p+4),BE(p+8),BE(p+12),BE(p+20),p[16],p[17],p[18],p[19]};if(!Valid(info))return bad("XEX title/disc number/count invalid");return {info,{}};
}
MetadataResult ReadXexExecutionInfo(const std::string&p){std::ifstream f(NativePath(p),std::ios::binary);if(!f)return {{},"Cannot read default.xex header"};std::array<uint8_t,24> fixed{};if(!f.read(reinterpret_cast<char*>(fixed.data()),fixed.size()))return {{},"Truncated default.xex"};uint32_t size=BE(fixed.data()+8);if(memcmp(fixed.data(),"XEX2",4)||size<24||size>2u<<20)return {{},"Unsupported/oversized XEX2 header"};std::vector<uint8_t>b(size);memcpy(b.data(),fixed.data(),24);if(!f.read(reinterpret_cast<char*>(b.data()+24),size-24))return {{},"Truncated XEX2 optional headers"};return ParseXexExecutionInfo(b.data(),b.size());}
std::string NormalizePath(const std::string&base,const std::string&entry,const std::vector<std::string>&roots){Need(!entry.empty(),Error::InvalidPath,"Empty media entry");std::string p=entry;std::replace(p.begin(),p.end(),'\\','/');Need(p.find('\0')==p.npos,Error::InvalidPath,"NUL in media path");auto absolute=(p[0]=='/'||Drive(p))?p:Lexical(base)+"/"+p;auto result=Lexical(absolute);Allowed(result,roots);return result;}
std::vector<std::string> ParseM3u(const std::string&input,const std::string&playlist,const Options&o){Need(input.size()<=65536,Error::MalformedPlaylist,"Playlist exceeds 64 KiB");Need(o.max_members>=1&&o.max_members<=255,Error::TooManyMembers,"Invalid member limit");auto pp=Lexical(playlist),dir=Parent(pp);auto roots=o.media_roots.empty()?std::vector<std::string>{dir}:o.media_roots;std::string text=input;if(text.compare(0,3,"\xef\xbb\xbf")==0)text.erase(0,3);Need(Utf8(text)&&text.find('\0')==text.npos,Error::MalformedPlaylist,"Playlist must be UTF8 without NUL");std::vector<std::string> paths;std::set<std::string> seen;size_t at=0;
 while(at<text.size()){size_t end=text.find('\n',at);if(end==text.npos)end=text.size();auto line=Trim(text.substr(at,end-at));Need(line.size()<=4096,Error::MalformedPlaylist,"Playlist line too long");at=end+1;if(line.empty())continue;if(line[0]=='#'){Need(line.compare(0,7,"#EXT-X-")!=0,Error::MalformedPlaylist,"HLS playlists are not Xbox media sets");continue;}
 if(line.front()=='"'){Need(line.size()>=2&&line.back()=='"',Error::MalformedPlaylist,"Unclosed quoted path");line=line.substr(1,line.size()-2);}Need(!line.empty(),Error::MalformedPlaylist,"Empty quoted path");auto p=NormalizePath(dir,line,roots);Need(Extension(p)!=".m3u"&&Extension(p)!=".m3u8",Error::MalformedPlaylist,"Nested playlists are unsupported");Need(seen.insert(Key(p)).second,Error::DuplicatePath,"Duplicate normalized playlist path");Need(paths.size()<o.max_members,Error::TooManyMembers,"Playlist member limit exceeded");paths.push_back(std::move(p));
 }Need(!paths.empty(),Error::MalformedPlaylist,"Playlist has no media entries");return paths;
}
DiscSet DiscoverDiscSet(const std::string&launch,unsigned requested,const Options&o,const Probe&probe){DiscSet set;try{Need(requested>=1&&requested<=255,Error::InvalidMetadata,"Requested disc must be 1..255");Need(o.max_members>=1&&o.max_members<=255&&o.max_directory_entries>0,Error::TooManyMembers,"Invalid discovery limits");auto lexical=Lexical(launch);std::error_code ec;bool isdir=fs::is_directory(NativePath(lexical),ec);auto initial_dir=isdir?lexical:Parent(lexical);auto folder_hint=DiscHint(Name(initial_dir));auto image_hint=DiscHint(Name(lexical));
 // Extracted DiscN/default.xex searches its physical parent for discs.m3u and siblings.
 std::string family_dir=folder_hint.number?Parent(initial_dir):initial_dir;
 auto roots=o.media_roots.empty()?std::vector<std::string>{family_dir}:o.media_roots;
 for(auto&r:roots)r=Lexical(r);family_dir=Canonical(family_dir,roots);auto launched=MountInput(lexical,roots,o);auto baseline=ProbeInput(launched,probe);Need(baseline.info.has_value(),Error::MissingMetadata,"Launched media metadata unavailable");Need(Valid(*baseline.info),Error::InvalidMetadata,"Launched media metadata invalid");
 std::vector<std::string> playlists;std::vector<std::string> search={initial_dir};if(family_dir!=initial_dir)search.push_back(family_dir);
 for(auto&dir:search)for(auto&e:Entries(dir,o.max_directory_entries))if(Lower(PathText(e.path().filename()))=="discs.m3u")playlists.push_back(Canonical(PathText(e.path()),roots));
 Need(playlists.size()<=1,Error::AmbiguousPlaylist,"Multiple adjacent discs.m3u files; choose one explicitly");std::vector<std::pair<std::string,unsigned>> inputs;
 if(!playlists.empty()){set.source=playlists[0];Options playlist_options=o;playlist_options.media_roots=roots;for(auto&p:ParseM3u(Text(playlists[0],65536),playlists[0],playlist_options))inputs.push_back({p,0});}
 else {set.source=family_dir;for(auto&e:Entries(family_dir,o.max_directory_entries)){std::string p=PathText(e.path());std::error_code se;bool dir=e.is_directory(se);Need(!se,Error::MissingMedia,"Cannot inspect sibling");auto hint=DiscHint(PathText(e.path().filename()));if(!hint.number)continue;
   if(folder_hint.number){if(hint.family!=folder_hint.family || (!dir && Extension(p)!=".iso" && Extension(p)!=".zar"))continue;}
   else {if(!image_hint.number || dir || (Extension(p)!=".iso"&&Extension(p)!=".zar") || hint.family!=image_hint.family)continue;}
   Need(inputs.size()<o.max_members,Error::TooManyMembers,"Sibling disc member limit exceeded");inputs.push_back({p,hint.number});}
  // No naming evidence: return only launched media; never scan unrelated titles by title ID.
  if(inputs.empty())inputs.push_back({launched,0});
 }
 std::set<std::string> seen;std::set<unsigned> numbers;size_t launched_count=0;
 for(size_t i=0;i<inputs.size();++i){auto p=MountInput(inputs[i].first,roots,o);Need(seen.insert(Key(p)).second,Error::DuplicatePath,"Multiple entries resolve to the same media");auto metadata=Key(p)==Key(launched)?baseline:ProbeInput(p,probe);Need(metadata.info.has_value(),Error::MissingMetadata,"Candidate metadata unavailable; no automatic swap allowed");auto info=*metadata.info;Need(Valid(info),Error::InvalidMetadata,"Candidate title/disc number/count invalid");Need(Family(info,*baseline.info),Error::IdentityMismatch,"Candidate differs in title/count/version/save/platform identity");Need(!inputs[i].second||inputs[i].second==info.disc_number,Error::DiscNumberMismatch,"Disc filename disagrees with XEX disc_number");Need(numbers.insert(info.disc_number).second,Error::AmbiguousDisc,"Multiple media candidates claim the same disc_number");bool active=Key(p)==Key(launched);launched_count+=active;set.candidates.push_back({p,info,i,inputs[i].second,active});}
 Need(launched_count==1,Error::LaunchedDiscMissing,"Playlist/family must contain the exact launched medium once");std::sort(set.candidates.begin(),set.candidates.end(),[](const Candidate&a,const Candidate&b){if(a.execution.disc_number!=b.execution.disc_number)return a.execution.disc_number<b.execution.disc_number;return a.path<b.path;});
 set.complete=set.candidates.size()==baseline.info->disc_count;for(size_t i=0;i<set.candidates.size();++i)if(set.candidates[i].execution.disc_number==requested)set.requested_index=i;
 Need(set.requested_index.has_value(),Error::RequestedDiscMissing,"Requested disc is missing from this verified set");
 }catch(const Failure&e){set.error=e.code;set.detail=e.what();set.requested_index.reset();}catch(const std::exception&){set.error=Error::MissingMedia;set.detail="Filesystem/probe failure; disc selection refused";set.requested_index.reset();}return set;
}
bool ValidateTarget(const Candidate&selected,const ExecutionInfo&running,unsigned requested,const ExecutionInfo&fresh,std::string&reason){if(!Valid(running)||!Valid(fresh)||requested<1||requested>running.disc_count){reason="Invalid running/requested/candidate disc metadata";return false;}if(!Family(running,fresh)||fresh.disc_number!=requested){reason="Target identity/requested disc mismatch";return false;}if(!Same(selected.execution,fresh)){reason="Selected target metadata changed; rediscover before swap";return false;}reason.clear();return true;}
const char* ErrorName(Error e){switch(e){
#define E(n) case Error::n:return #n;
 E(None) E(InvalidPath) E(UnsupportedMedia) E(MissingMedia) E(MalformedPlaylist) E(AmbiguousPlaylist) E(DuplicatePath) E(TooManyMembers) E(MissingMetadata) E(InvalidMetadata) E(IdentityMismatch) E(DiscNumberMismatch) E(AmbiguousDisc) E(LaunchedDiscMissing) E(RequestedDiscMissing)
#undef E
 }return "Unknown";}
}

namespace xbox360ps5::multidisc {
std::vector<std::filesystem::path> CandidatesForDisc(
    const std::filesystem::path& launch, uint32_t requested, std::string& error) {
  using namespace ps5x360_disc;
  error.clear();
  try {
    Need(requested>=1 && requested<=255, Error::InvalidMetadata,
         "Requested disc must be 1..255");
    Options options; options.max_members=16;
    auto lexical=Lexical(PathText(launch));
    std::error_code ec;
    bool isdir=std::filesystem::is_directory(NativePath(lexical),ec);
    Need(!ec,Error::MissingMedia,"Cannot inspect launched path");
    auto initial_dir=isdir?lexical:Parent(lexical);
    auto folder_hint=DiscHint(Name(initial_dir));
    auto image_hint=DiscHint(Name(lexical));
    auto family_dir=folder_hint.number?Parent(initial_dir):initial_dir;
    std::vector<std::string> roots={family_dir};
    // Resolve roots first, so legitimate symlink aliases retain canonical containment.
    auto physical=std::filesystem::canonical(NativePath(family_dir),ec);
    Need(!ec,Error::MissingMedia,"Physical media family is unavailable");
    family_dir=Lexical(PathText(physical));roots={family_dir};
    auto launched=MountInput(lexical,roots,options);
    std::vector<std::string> playlists;
    std::vector<std::string> search={initial_dir};
    if(Key(initial_dir)!=Key(family_dir))search.push_back(family_dir);
    std::set<std::string> searched;
    for(const auto&dir:search){
      auto canonical_dir=Canonical(dir,roots);
      if(!searched.insert(Key(canonical_dir)).second)continue;
      for(const auto&e:Entries(canonical_dir,options.max_directory_entries))
        if(Lower(PathText(e.path().filename()))=="discs.m3u")
          playlists.push_back(Canonical(PathText(e.path()),roots));
    }
    Need(playlists.size()<=1,Error::AmbiguousPlaylist,
         "Multiple adjacent discs.m3u files; explicit set is ambiguous");
    std::vector<std::pair<std::string,unsigned>> inputs;
    if(!playlists.empty()){
      options.media_roots=roots;
      for(const auto&p:ParseM3u(Text(playlists[0],65536),playlists[0],options)){
        auto path=MountInput(p,roots,options);
        unsigned hint=DiscHint(Name(path)).number;
        if(!hint && Lower(Name(path))=="default.xex")hint=DiscHint(Name(Parent(path))).number;
        inputs.push_back({path,hint});
      }
    }else{
      for(const auto&e:Entries(family_dir,options.max_directory_entries)){
        auto path=PathText(e.path());auto hint=DiscHint(PathText(e.path().filename()));
        if(!hint.number)continue;
        bool directory=e.is_directory(ec);Need(!ec,Error::MissingMedia,"Cannot inspect sibling");
        if(folder_hint.number){
          if(hint.family!=folder_hint.family ||
             (!directory && Extension(path)!=".iso" && Extension(path)!=".zar"))continue;
        }else{
          if(!image_hint.number || directory || hint.family!=image_hint.family ||
             (Extension(path)!=".iso" && Extension(path)!=".zar"))continue;
        }
        Need(inputs.size()<16,Error::TooManyMembers,"Disc set exceeds 16 candidates");
        inputs.push_back({MountInput(path,roots,options),hint.number});
      }
      if(inputs.empty()){
        Need(requested==1,Error::RequestedDiscMissing,"No associated disc set for requested disc");
        inputs.push_back({launched,0});
      }
    }
    std::set<std::string> seen;std::set<unsigned> numbered;
    unsigned launched_count=0;bool unknown=false;
    for(const auto&i:inputs){
      Need(seen.insert(Key(i.first)).second,Error::DuplicatePath,"Duplicate canonical media in explicit set");
      launched_count+=Key(i.first)==Key(launched);
      if(i.second)Need(numbered.insert(i.second).second,Error::AmbiguousDisc,
                     "Multiple structural candidates claim one disc number");
      else unknown=true;
    }
    Need(launched_count==1,Error::LaunchedDiscMissing,"Set must contain exact launched medium once");
    Need(unknown || numbered.count(requested),Error::RequestedDiscMissing,"Requested disc absent from named set");
    std::sort(inputs.begin(),inputs.end(),[](const auto&a,const auto&b){
      auto na=a.second?a.second:256,nb=b.second?b.second:256;
      return na!=nb?na<nb:a.first<b.first;
    });
    std::vector<std::filesystem::path> result;
    for(const auto&i:inputs)result.push_back(NativePath(i.first));
    return result;
  }catch(const Failure&e){error=std::string(ErrorName(e.code))+": "+e.what();}
   catch(const std::exception&){error="Filesystem failure; structural disc candidates refused";}
  return {};
}
}
