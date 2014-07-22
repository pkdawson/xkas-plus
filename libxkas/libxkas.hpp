/*
  xkas
  version: 0.14 (2010-10-19)
  author: byuu
  license: public domain
*/

#ifndef LIBXKAS_HPP
#define LIBXKAS_HPP

#include <nall/array.hpp>
#include <nall/file.hpp>
#include <nall/foreach.hpp>
#include <nall/function.hpp>
#include <nall/stdint.hpp>
#include <nall/string.hpp>
#include <nall/vector.hpp>
using namespace nall;

struct xkas;

struct xkasArch {
  virtual void init(unsigned pass) = 0;
  virtual unsigned archaddr(unsigned fileaddr) = 0;
  virtual unsigned fileaddr(unsigned archaddr) = 0;
  virtual bool assemble_command(string&) = 0;
  xkasArch(xkas &self) : self(self) {}
  xkas &self;
};

#include "arch/none.hpp"
#include "arch/gba.thumb.hpp"
#include "arch/snes.cpu.hpp"

struct xkas {
  bool open(const char *filename);
  bool assemble(const char *filename);
  void close();
  xkas();

private:
  enum Pass { pass_read = 1, pass_assemble = 2 };
  unsigned pass;
  string error;
  string warning;

  //file manipulation
  file binary;
  void write(uint8_t data);

  bool assemble_file(const char*);
  void assemble_defines(string&);
  bool assemble_command(string&);

  //arch
  enum Endian : unsigned { endian_lsb, endian_msb };
  unsigned endian;
  xkasArch *arch;
  xkasNone arch_none;
  xkasGBATHUMB arch_gba_thumb;
  xkasSNESCPU arch_snes_cpu;
  friend class xkasNone;
  friend class xkasGBATHUMB;
  friend class xkasSNESCPU;

  struct Define {
    string name;
    string value;
  };

  struct Label {
    string name;
    unsigned offset;
  };

  //state
  unsigned pc() const;  //return program counter (base address)

  struct State {
    unsigned org;
    unsigned base;
    linear_vector<Define> define;
    linear_vector<Label> label;
    string active_namespace;
    Label active_label;
    unsigned plus_label_counter;
    unsigned minus_label_counter;
    uint64_t table[256];
  } state;

  //tool
  int eval_integer(const char *&s);
  int eval(const char *&s, int depth = 0);
  int decode(const char *s);
  bool decode_label(string &s);
  bool find_label(string &name, unsigned &offset);
};

#endif
