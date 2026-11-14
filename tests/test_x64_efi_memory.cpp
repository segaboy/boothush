#include "x64_efi_memory.h"
#include "x64_media_internal.h"
#include "x64_pe_loader.h"
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using u64 = std::uint64_t;
void check(bool b, const char *s) { if (!b) { std::cerr << s << '\n'; std::exit(1); } }
template<std::size_t N> void put(std::array<std::uint8_t,N> &a, size_t p, u64 v, unsigned n=8) {
    for (unsigned i=0;i<n;++i) a[p+i]=std::uint8_t(v>>(i*8));
}
template<std::size_t N> u64 get(const std::array<std::uint8_t,N> &a, size_t p, unsigned n=8) {
    u64 v=0; for (unsigned i=0;i<n;++i) v|=u64(a[p+i])<<(i*8); return v;
}
int main() {
    X64BootServices model(0x100000,256); std::array<std::uint8_t,1024> ram{};
    X64EfiMemory e(model,ram,0x1234); using E=X64EfiMemory;
    put(ram,8,0); put(ram,32,0xaabbccdd);
    check(e.get_memory_map(8,0,0,0,0)==E::buffer_too_small && get(ram,8)==40,
          "short map query must return required bytes");
    check(get(ram,32)==0xaabbccdd,"short map query clobbered unrelated output");
    put(ram,8,40); auto before=ram;
    check(e.get_memory_map(8,1000,32,48,64)==E::invalid_parameter && ram==before,
          "out-of-bounds map was not atomic");
    check(e.get_memory_map(8,32,32,48,64)==E::invalid_parameter && ram==before,
          "overlapping outputs accepted");
    check(e.get_memory_map(8,128,32,48,64)==E::success,"initial map failed");
    check(get(ram,8)==40 && get(ram,32)==1 && get(ram,48)==40 && get(ram,64,4)==1,
          "map metadata incorrect");
    check(get(ram,128,4)==7 && get(ram,132,4)==0 && get(ram,136)==0x100000 &&
          get(ram,144)==0 && get(ram,152)==256 && get(ram,160)==0,"descriptor layout incorrect");
    const u64 old_key=get(ram,32);
    put(ram,80,0x17ffff); before=ram;
    check(e.allocate_pages(0,2,2,80)==E::unsupported && ram==before,"unsupported allocation changed RAM");
    check(e.allocate_pages(1,2,2,80)==E::success && get(ram,80)==0x17e000,"allocate max failed");
    check(e.exit_boot_services(0x1234,old_key)==E::invalid_parameter && !model.exited(),"stale key accepted");
    put(ram,8,40); check(e.get_memory_map(8,128,32,48,64)==E::buffer_too_small && get(ram,8)==120,
          "split map size incorrect");
    check(e.get_memory_map(8,128,32,48,64)==E::success && get(ram,32)!=old_key,"updated map failed");
    check(get(ram,128,4)==7 && get(ram,168,4)==2 && get(ram,176)==0x17e000 &&
          get(ram,192)==2 && get(ram,208,4)==7,"split descriptor encoding incorrect");
    check(e.exit_boot_services(0x9999,get(ram,32))==E::invalid_parameter && !model.exited(),"wrong handle accepted");
    check(e.free_pages(0x17e000,2)==E::success,"free failed");
    check(e.exit_boot_services(0x1234,get(ram,32))==E::invalid_parameter,"free did not stale key");
    put(ram,8,40); check(e.get_memory_map(8,128,32,48,64)==E::success,"map after free failed");
    check(e.exit_boot_services(0x1234,get(ram,32))==E::success && model.exited(),"exit failed");
    before=ram; check(e.get_memory_map(8,128,32,48,64)==E::invalid_parameter && ram==before,
                      "post-exit map changed RAM");
    check(e.allocate_pages(1,2,1,80)==E::invalid_parameter,"post-exit allocation succeeded");
    check(e.free_pages(0x100000,1)==E::invalid_parameter,"post-exit free succeeded");
    X64BootServices image_model(0x100000,256);
    check(image_model.reserve_image(0x104000,3,X64BootServices::Type::loader_code)==X64BootServices::Status::ok,
          "image model setup failed");
    X64EfiMemory image_efi(image_model,ram,0x1234);
    put(ram,8,120);
    check(image_efi.get_memory_map(8,128,32,48,64)==E::success && get(ram,8)==120 &&
          get(ram,168,4)==1 && get(ram,176)==0x104000 && get(ram,192)==3,
          "loader code descriptor was not serialized");

    if (const char *path=std::getenv("BOOTHUSH_CLASSICK_X64_DISK")) {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        check(bool(file),"optional candidate disk unavailable");
        check(file.tellg()==std::streampos(67108864),"optional candidate disk size unsupported");
        file.seekg(0);
        std::vector<std::uint8_t> disk((std::istreambuf_iterator<char>(file)),{}), payload;
        check(disk.size()==67108864,"candidate disk read incomplete");
        x64_media_info info{};
        check(!x64_media_extract_efi(disk.data(),disk.size(),info,payload),"candidate extraction failed");
        X64PeImage image;
        check(!image.load(payload,0x10000000),"candidate mapping failed");
        check(!image.bytes().empty() && image.bytes().size()%4096==0,"unexpected image size");
        X64BootServices candidate(0x100000,0x20000);
        const u64 pages=image.bytes().size()/4096;
        check(candidate.reserve_image(image.loaded_base(),pages,X64BootServices::Type::loader_code)==X64BootServices::Status::ok,
              "candidate image range reservation failed");
        X64EfiMemory candidate_efi(candidate,ram,0x1234);
        put(ram,8,120);
        check(candidate_efi.get_memory_map(8,128,32,48,64)==E::success &&
              get(ram,168,4)==1 && get(ram,176)==image.loaded_base() && get(ram,192)==pages,
              "candidate image descriptor incorrect");
        put(ram,80,0x10000fff);
        check(candidate_efi.allocate_pages(1,2,1,80)==E::success && get(ram,80)==0x0ffff000,
              "candidate allocation overlapped image");
        std::cout << "candidate EFI image: " << pages << " modeled pages; no guest execution\n";
    } else std::cout << "candidate image integration skipped: BOOTHUSH_CLASSICK_X64_DISK unset\n";
}
