/*
Copyright (c) 2014, Trail of Bits
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

  Redistributions in binary form must reproduce the above copyright notice, this  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

  Neither the name of Trail of Bits nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <LExcn.h>
#include <boost/filesystem.hpp>
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Debug.h"
#include "ELFTarget.h"
#include "../common/to_string.h"

using namespace std;
using namespace llvm;
using namespace boost;

ElfTarget* ElfTarget::CreateElfTarget(string f, const Target *T)
{
    auto err_or_buff = MemoryBuffer::getFile(f);
    std::error_code ec = err_or_buff.getError();
    LASSERT(!ec, "Can't get memory buffer");

    std::string mn = filesystem::path(f).stem().string();
    std::string target(T->getName());

    if(!target.compare("x86-64")){
        return new ElfTarget(
                mn,
                new llvm::object::ELF64LEObjectFile (std::move(err_or_buff.get()), ec),
                target
                );
    } else {
        return new ElfTarget(
               mn,
               new llvm::object::ELF32LEObjectFile (std::move(err_or_buff.get()), ec),
               target
               );
    }
    std::cout.flush();
}

bool ElfTarget::getEntryPoint(::uint64_t &ep) const
{
    std::error_code ec;
    if(!T.compare("x86-64")){
        ec = this->elf_obj->getEntryPoint(ep);
    } else {
        ec = this->elf_obj32->getEntryPoint(ep);
    }
    return ec == object::object_error::success && ep != 0;
}

static bool find_import_for_addr(object::SectionRef section, uint32_t offt, uint32_t target,
        std::string &import_name) {

    llvm::object::relocation_iterator rit = section.relocation_begin();
    std::error_code ec;

    while( rit != section.relocation_end() ) {
        llvm::object::SymbolRef       symref;
        VA                            addr = 0;

        ec = rit->getAddress((::uint64_t &)addr);
        LASSERT(!ec, "Can't get address for relocation ref");
        llvm::dbgs() << "\t" << __FUNCTION__ << ": Testing " << to_string<VA>(target, hex)
            << " vs. " << to_string<VA>(addr+offt, hex) << "\n";

        if( target == (addr+offt) ) {

            llvm::object::SymbolRef       symref;
            symref = *rit->getSymbol();

            llvm::StringRef strr;
            ec = symref.getName(strr);
            LASSERT(!ec, "Can't get name for symbol ref");

            import_name = strr.str();
            llvm::dbgs() << "Found symbol named: " << import_name << "\n";

            ::uint64_t sym_addr;
            ec = symref.getAddress(sym_addr);

			//::int64_t addend;
			//ec = getELFRelocationAddend(*rit, addend);
			//sym_addr += addend;

            if(ec) {
                llvm::dbgs() << "Could not get address of symbol: " << import_name << "\n";
            } else {
                llvm::dbgs() << "Address for " << import_name
                             << " is: " << to_string< ::uint64_t >(sym_addr, hex) << "\n";
            }

            llvm::object::SymbolRef::Type symtype;
            ec = symref.getType(symtype);
            switch(symtype) {
                case llvm::object::SymbolRef::ST_Unknown:
                case llvm::object::SymbolRef::ST_Data:
                case llvm::object::SymbolRef::ST_Function:
                    if( sym_addr == (::uint64_t)(-1) ) {
                        return true;
                    } else {
                        llvm::dbgs() << "Skipping symbol due to address\n";
                    }
                    break;
				//case llvm::object::SymbolRef::ST_Debug:
					//return true;
					//break;
                default:
                    llvm::dbgs() << "Skipping symbol since its probably not an import! " <<symtype << "\n";
            }
        }

        ++rit;
    }


    return false;
}

bool ElfTarget::find_in_any_section(uint32_t target, std::string &import_name)
{
      for(vector<secT>::iterator it = this->secs.begin(), en = this->secs.end();
          it != en;
          ++it)
      {
          object::SectionRef  sec = (*it).second;
          StringRef sr;
          sec.getName(sr);
          if(find_import_for_addr(sec, 0, target, import_name)) {
              return true;
          }
      }

      return false;
}

bool ElfTarget::find_import_name(uint32_t addrToFind, std::string &import_name)
{
    if(!T.compare("x86-64")){
        LASSERT(this->elf_obj != NULL, "ELF Object File not initialized");
    } else {
        LASSERT(this->elf_obj32 != NULL, "ELF Object File not initialized");
    }


    uint32_t  offt;
    object::SectionRef section;

    bool      found_offt = getSectionForAddr(this->secs, addrToFind, section, offt);
    if (false == found_offt) {
        return false;
    }

    uint32_t final_target = addrToFind;

    section = this->fixRelocationSection(section);

    if(find_import_for_addr(section, offt, final_target, import_name)) {
        return true;
    }

    if(this->isLinked()) {

        if(find_in_any_section(final_target, import_name)) {
            return true;
        }

        llvm::dbgs() << __FUNCTION__ << ": Doing extra deref" << "\n";
        // one more level of indirection for fully linked binaries
        uint8_t *ft_ptr = (uint8_t*)&final_target;
        this->readByte(addrToFind+0, ft_ptr+0);
        this->readByte(addrToFind+1, ft_ptr+1);
        this->readByte(addrToFind+2, ft_ptr+2);
        this->readByte(addrToFind+3, ft_ptr+3);
        if(find_import_for_addr(section, offt, final_target, import_name)) {
            return true;
        }

        if(find_in_any_section(final_target, import_name)) {
            return true;
        }
    }


    LASSERT(found_offt, "Address "+to_string<uint32_t>(final_target, hex) + " not found");
    return false;
}

bool ElfTarget::isLinked() const {
    // partially linked objects have no entry point
    ::uint64_t dummy;
    return true == this->getEntryPoint(dummy);
}

bool ElfTarget::is_in_code(VA addr) const {

    uint32_t offt;
    uint32_t addr32 = (uint32_t)(addr);
    object::SectionRef section;

    bool      found_offt = getSectionForAddr(this->secs, addr32, section, offt);
    if (false == found_offt) {
        return false;
    }

    std::error_code e;
    bool is_text_ref;
    e = section.isText(is_text_ref);

    LASSERT(!e, e.message());

    return is_text_ref;
}

bool ElfTarget::is_in_data(VA addr) const {

    uint32_t offt;
    uint32_t addr32 = (uint32_t)(addr);
    object::SectionRef section;

    bool      found_offt = getSectionForAddr(this->secs, addr32, section, offt);
    if (false == found_offt) {
        return false;
    }

    std::error_code e;
    bool is_data, is_bss;
    e = section.isData(is_data);
    LASSERT(!e, e.message());

    e = section.isBSS(is_bss);
    LASSERT(!e, e.message());

    return is_data || is_bss;
}
