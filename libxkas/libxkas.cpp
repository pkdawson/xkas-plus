#include "libxkas.hpp"
#include "tool.cpp"

#include "arch/none.cpp"
#include "arch/gba.thumb.cpp"
#include "arch/snes.cpu.cpp"

bool xkas::open(const char *filename) {
  //try and open existing file
  if(binary.open(filename, file::mode::readwrite) == false) {
    //if unable to open, try creating a new file
    if(binary.open(filename, file::mode::write) == false) {
      //if unable to create, fail
      return false;
    }
  }
  return true;
}

void xkas::close() {
  binary.close();
}

bool xkas::assemble(const char *filename) {
  for(pass = 1; pass <= 2; pass++) {
    //initialize
    endian = endian_lsb;
    arch = &arch_none;
    error = "";
    warning = "";

    //state
    state.org = 0;
    state.base = 0;
    state.define.reset();
    state.active_namespace = "global";
    state.active_label.name = "";
    state.plus_label_counter = 1;
    state.minus_label_counter = 1;
    for(unsigned i = 0; i < 256; i++) state.table[i] = i;

    //arch
    arch_none.init(pass);
    arch_gba_thumb.init(pass);
    arch_snes_cpu.init(pass);

    if(assemble_file(filename) == false) return false;
  }

  return true;
}

xkas::xkas() : arch_none(*this), arch_gba_thumb(*this), arch_snes_cpu(*this) {
}

//========
//internal
//========

unsigned xkas::pc() const {
  return state.base;
}

bool xkas::assemble_file(const char *filename) {
  string data;
  if(data.readfile(filename) == false) return false;
  data.replace("\r", "");
  lstring line;
  line.split("\n", data);

  for(unsigned l = 0; l < line.size(); l++) {
    if(auto position = qstrpos(line[l], "//")) line[l][position()] = 0;  //strip comments
    line[l].qreplace("\t", " ");
    while(qstrpos(line[l], "  ")) line[l].qreplace("  ", " ");  //drop extra whitespace
    line[l].qreplace(", ", ",");
    assemble_defines(line[l]);

    lstring block;
    block.qsplit(";", line[l]);
    for(unsigned b = 0; b < block.size(); b++) {
      block[b].trim(" ");  //trim start and end whitespace
      if(block[b] == "") continue;  //ignore blank blocks
      if(assemble_command(block[b]) == false) {
        print("xkas error: pass ", pass, ", line ", l + 1, ":", b + 1, ": \"", block[b], "\"\n");
        if(error != "") print(error, "\n");  //print detailed error message, if one exists
        return false;
      }
    }
  }

  return true;
}

//scan string for {define} values, and replace them.
//this function scans inside quoted strings.
void xkas::assemble_defines(string &s) {
  start:
  unsigned length = strlen(s);
  for(unsigned i = 0; i < length; i++) {
    if(s[i] == '{') {
      size_t start = ++i;
      while(i < length && s[i] != '}') i++;
      if(s[i] != '}') return;  //no more defines to resolve
      size_t end = i++;
      string name = substr(s, start, end - start);
      if(!strpos(name, "::")) {
        //add current namespace prefix if none explicitly specified
        name = { state.active_namespace, "::", name };
      }
      for(unsigned i = 0; i < state.define.size(); i++) {
        if(name == state.define[i].name) {
          s = string(
            substr(s, 0, start - 1),  //-1 = exclude '{'
            state.define[i].value,
            substr(s, end + 1)        //+1 = exclude '}'
          );
          goto start;  //restart define decode (allows recursive defines)
        }
      }
    }
  }
}

//returning false will abort assembly process
bool xkas::assemble_command(string &s) {
  lstring part;
  part.qsplit(" ", s);

  //========
  //= arch =
  //========
  if(part[0] == "arch" && part.size() == 2) {
    //reset address, as different archs may use different org / base adjustments
    state.org = 0;
    state.base = 0;

    if(part[1] == "none") {
      endian = endian_lsb;
      arch = &arch_none;
      return true;
    } else if(part[1] == "gba.thumb") {
      endian = endian_lsb;
      arch = &arch_gba_thumb;
      return true;
    } else if(part[1] == "snes.cpu") {
      endian = endian_lsb;
      arch = &arch_snes_cpu;
      return true;
    }

    error = "specified arch unrecognized";
    return false;
  }

  //==========
  //= endian =
  //==========
  if(part[0] == "endian" && part.size() == 2) {
    if(part[1] == "lsb") {
      endian = endian_lsb;
      return true;
    }
    if(part[1] == "msb") {
      endian = endian_msb;
      return true;
    }
    error = "specified endian mode unrecognized";
    return false;
  }

  //==========
  //= incsrc =
  //==========
  if(part[0] == "incsrc" && part.size() == 2) {
    part[1].trim<1>("\"");
    return assemble_file(part[1]);
  }

  //==========
  //= incbin =
  //==========
  if(part[0] == "incbin" && part.size() == 2) {
    part[1].trim<1>("\"");
    file fp;
    if(fp.open(part[1], file::mode::read) == false) {
      error = "file not found";
      return false;
    }
    for(unsigned i = 0; i < fp.size(); i++) write(fp.read());
    fp.close();
    return true;
  }

  //=======
  //= org =
  //=======
  if(part[0] == "org" && part.size() == 2) {
    //individual archs may have custom mappers which obviate the need for base;
    //therefore org sets base, so that base is not needed every time org is used
    state.org = state.base = decode(part[1]);
    if(pass == 2) binary.seek(arch->fileaddr(state.org));
    return true;
  }

  //========
  //= base =
  //========
  if(part[0] == "base" && part.size() == 2) {
    state.base = decode(part[1]);
    return true;
  }

  //=========
  //= align =
  //=========
  if(part[0] == "align" && part.size() == 2) {
    unsigned align = decode(part[1]);
    while(state.base % align) write(0x00);
    return true;
  }

  //==========
  //= loadpc =
  //==========
  if(part[0] == "loadpc" && part.size() == 2) {
    part[1].trim<1>("\"");
    file fp;
    if(fp.open(part[1], file::mode::read) == false) {
      error = "file not found";
      return false;
    }
    state.org  = fp.read() << 24;
    state.org |= fp.read() << 16;
    state.org |= fp.read() <<  8;
    state.org |= fp.read() <<  0;
    fp.close();
    return true;
  }

  //==========
  //= savepc =
  //==========
  if(part[0] == "savepc" && part.size() == 2) {
    part[1].trim<1>("\"");
    file fp;
    if(fp.open(part[1], file::mode::write) == false) {
      error = "cannot open file";
      return false;
    }
    fp.write(state.org >> 24);
    fp.write(state.org >> 16);
    fp.write(state.org >>  8);
    fp.write(state.org >>  0);
    fp.close();
    return true;
  }

  //========
  //= fill =
  //========
  if(part[0] == "fill" && part.size() == 2) {
    lstring subpart;
    subpart.split(",", part[1]);
    unsigned length = decode(subpart[0]);
    uint8_t n = (subpart.size() == 1 ? 0x00 : decode(subpart[1]));
    for(unsigned i = 0; i < length; i++) write(n);
    return true;
  }

  //==========
  //= fillto =
  //==========
  if(part[0] == "fillto" && part.size() == 2) {
    lstring subpart;
    subpart.split(",", part[1]);
    unsigned offset = decode(subpart[0]);
    uint8_t n = (subpart.size() == 1 ? 0x00 : decode(subpart[1]));
    while(arch->archaddr(binary.offset()) < offset) write(n);
    return true;
  }

  //======
  //= db =
  //======
  if(part[0] == "db" && part.size() == 2) {
    lstring subpart;
    subpart.qsplit(",", part[1]);
    for(unsigned i = 0; i < subpart.size(); i++) {
      if(subpart[i].wildcard("\"*\"")) {
        //quoted string
        subpart[i].trim<1>("\"");
        for(unsigned l = 0; l < strlen(subpart[i]); l++) {
          uint8_t value = subpart[i][l];
          write(state.table[value]);
        }
      } else {
        //math equation
        write(decode(subpart[i]));
      }
    }
    return true;
  }

  //======
  //= dw =
  //======
  if(part[0] == "dw" && part.size() == 2) {
    lstring subpart;
    subpart.qsplit(",", part[1]);
    for(unsigned i = 0; i < subpart.size(); i++) {
      if(subpart[i].wildcard("\"*\"")) {
        //quoted string
        subpart[i].trim<1>("\"");
        for(unsigned l = 0; l < strlen(subpart[i]); l++) {
          uint8_t value = subpart[i][l];
          if(endian == endian_lsb) {
            write(state.table[value]);
            write(state.table[value] >> 8);
          } else {
            write(state.table[value] >> 8);
            write(state.table[value]);
          }
        }
      } else {
        uint16_t n = decode(subpart[i]);
        if(endian == endian_lsb) {
          write(n >> 0);
          write(n >> 8);
        } else {
          write(n >> 8);
          write(n >> 0);
        }
      }
    }
    return true;
  }

  //======
  //= dl =
  //======
  if(part[0] == "dl" && part.size() == 2) {
    lstring subpart;
    subpart.qsplit(",", part[1]);
    for(unsigned i = 0; i < subpart.size(); i++) {
      if(subpart[i].wildcard("\"*\"")) {
        //quoted string
        subpart[i].trim<1>("\"");
        for(unsigned l = 0; l < strlen(subpart[i]); l++) {
          uint8_t value = subpart[i][l];
          if(endian == endian_lsb) {
            write(state.table[value]);
            write(state.table[value] >> 8);
            write(state.table[value] >> 16);
          } else {
            write(state.table[value] >> 16);
            write(state.table[value] >> 8);
            write(state.table[value]);
          }
        }
      } else {
        uint32_t n = decode(subpart[i]);
        if(endian == endian_lsb) {
          write(n >>  0);
          write(n >>  8);
          write(n >> 16);
        } else {
          write(n >> 16);
          write(n >>  8);
          write(n >>  0);
        }
      }
    }
    return true;
  }

  //======
  //= dd =
  //======
  if(part[0] == "dd" && part.size() == 2) {
    lstring subpart;
    subpart.qsplit(",", part[1]);
    for(unsigned i = 0; i < subpart.size(); i++) {
      if(subpart[i].wildcard("\"*\"")) {
        //quoted string
        subpart[i].trim<1>("\"");
        for(unsigned l = 0; l < strlen(subpart[i]); l++) {
          uint8_t value = subpart[i][l];
          if(endian == endian_lsb) {
            write(state.table[value]);
            write(state.table[value] >> 8);
            write(state.table[value] >> 16);
            write(state.table[value] >> 24);
          } else {
            write(state.table[value] >> 24);
            write(state.table[value] >> 16);
            write(state.table[value] >> 8);
            write(state.table[value]);
          }
        }
      } else {
        uint32_t n = decode(subpart[i]);
        if(endian == endian_lsb) {
          write(n >>  0);
          write(n >>  8);
          write(n >> 16);
          write(n >> 24);
        } else {
          write(n >> 24);
          write(n >> 16);
          write(n >>  8);
          write(n >>  0);
        }
      }
    }
    return true;
  }

  //==========
  //= define =
  //==========
  if(part[0] == "define" && part.size() == 3) {
    if(part[1].wildcard("'?'")) {
      //define table entry
      uint8_t index = part[1][1];
      state.table[index] = decode(part[2]);
      return true;
    } else {
      //remove quotes from define value (if they exist)
      part[2].trim<1>("\"");

      string name;
      if(!strpos(part[1], "::")) {
        //add namespace prefix, if it does not exist already
        name = { state.active_namespace, "::", part[1] };
      } else {
        //direct copy
        name = part[1];
      }

      unsigned index = state.define.size();
      for(unsigned i = 0; i < index; i++) {
        if(name == state.define[i].name) {
          //redefine an existing define
          state.define[i].value = part[2];
          return true;
        }
      }

      //create a new define
      state.define[index].name = name;
      state.define[index].value = part[2];
      return true;
    }
  }

  //=========
  //= label =
  //=========
  if(part[0].endswith(":") && part.size() == 1) {
    part[0].rtrim<1>(":");
    if(decode_label(part[0]) == false) return false;
    //set as active label if at the global scope (eg not a sublabel)
    if(!strpos(part[0], ".")) state.active_label.name = part[0];

    if(pass == 2) return true;  //only bind new labels on first pass
    unsigned index = state.label.size();
    state.label[index].name = { state.active_namespace, "::", part[0] };
    state.label[index].offset = pc();
    return true;
  }

  //===========
  //= + label =
  //===========
  if(part[0] == "+" && part.size() == 1) {
    if(pass == 1) {
      unsigned index = state.label.size();
      state.label[index].name = { "+", (int)state.plus_label_counter };
      state.label[index].offset = pc();
    }
    state.plus_label_counter++;
    return true;
  }

  //===========
  //= - label =
  //===========
  if(part[0] == "-" && part.size() == 1) {
    if(pass == 1) {
      unsigned index = state.label.size();
      state.label[index].name = { "-", (int)state.minus_label_counter };
      state.label[index].offset = pc();
    }
    state.minus_label_counter++;
    return true;
  }

  //=============
  //= namespace =
  //=============
  if(part[0] == "namespace" && part.size() == 2) {
    state.active_namespace = part[1];
    return true;
  }

  //=========
  //= print =
  //=========
  if(part[0] == "print" && part.size() == 2) {
    if(pass == 2) {
      lstring subpart;
      subpart.qsplit(",", part[1]);
      for(unsigned i = 0; i < subpart.size(); i++) {
        if(subpart[i] == "org") {
          print("0x", strhex(state.org));
        } else if(subpart[i] == "base") {
          print("0x", strhex(state.base));
        } else {
          subpart[i].trim<1>("\"");
          print(subpart[i]);
        }
      }
      print("\n");
    }
    return true;
  }

  return arch->assemble_command(s);
}

void xkas::write(uint8_t data) {
  if(pass == 2) binary.write(data);
  state.org++;
  state.base++;
}
