#pragma once
#include "Arduino.h"
#include <string>
#include <ctype.h>
#include <stdlib.h>
#include <iostream>
inline unsigned long millis(){ return 0; }
inline bool isDigit(char c){ return c>='0'&&c<='9'; }
class String {
 public:
  std::string s;
  String(){} String(const char*c):s(c){} String(const std::string&c):s(c){}
  unsigned length() const { return s.size(); }
  void trim(){ size_t a=s.find_first_not_of(" \t\r\n"); size_t b=s.find_last_not_of(" \t\r\n"); s = a==std::string::npos? "" : s.substr(a,b-a+1);} 
  int indexOf(char c,int from=0) const { auto p=s.find(c,from); return p==std::string::npos?-1:(int)p; }
  String substring(int a) const { return String(s.substr(a)); }
  String substring(int a,int b) const { return String(s.substr(a,b-a)); }
  char operator[](unsigned i) const { return s[i]; }
  void toLowerCase(){ for(auto&c:s) c=tolower(c);} 
  long toInt() const { return atol(s.c_str()); }
  float toFloat() const { return atof(s.c_str()); }
  bool operator==(const char*c) const { return s==c; }
  String& operator+=(char c){ s+=c; return *this; }
  String& operator=(const char*c){ s=c; return *this; }
};

inline void SerialMock::print(const String&v){ if(!quiet) std::cout<<v.s; }
inline void SerialMock::println(const String&v){ if(!quiet) std::cout<<v.s<<"\n"; }
