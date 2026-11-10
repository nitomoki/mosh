/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

#ifndef OSCPOLICY_HPP
#define OSCPOLICY_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace Terminal {

/* Rules deciding which OSC sequences that mosh does not handle natively are
   carried from the server to the client terminal.

   Policy text has one rule per line:

     NUMBER PREFIX MODE [MAXLEN]

   NUMBER is the OSC number. PREFIX is matched against the text after
   "NUMBER;", or "*" to match anything. MODE is one of "drop", "latest"
   (forward when the text differs from the last forwarded text) or
   "always" (forward every occurrence, even if the text repeats). MAXLEN
   limits the length of the whole OSC string; longer sequences are
   dropped. The first matching rule wins; unmatched sequences are dropped.
   Lines starting with '#' and blank lines are ignored. */
class OSCPolicy
{
public:
  enum class Mode
  {
    Drop,
    Latest,
    Always
  };

  struct Rule
  {
    std::wstring number;
    std::wstring prefix; /* empty matches anything */
    Mode mode;
    size_t max_length;
  };

  static constexpr size_t MAX_RULES = 32;
  static constexpr size_t DEFAULT_MAX_LENGTH = 1024;
  /* The dispatcher keeps at most this many characters of an OSC string. */
  static constexpr size_t MAX_OSC_LENGTH = 16 * 1024;
  /* Carries the rule index and occurrence count of the OSC string that
     follows it in a server-to-client diff. Never sent to a real terminal. */
  static constexpr int META_NUMBER = 5379;

private:
  std::vector<Rule> rules;

public:
  OSCPolicy() : rules() {}

  /* Parses policy text. Invalid lines are skipped and described in errors. */
  static OSCPolicy parse( const std::string& text, std::vector<std::string>& errors );

  /* Policy text travels in an environment variable as hex. */
  static std::string encode_hex( const std::string& text );
  static bool decode_hex( const std::string& hex, std::string& text );

  bool empty( void ) const { return rules.empty(); }
  size_t size( void ) const { return rules.size(); }
  const Rule& rule( size_t index ) const { return rules.at( index ); }

  /* Returns the index of the first rule matching the OSC string, or -1. */
  int match( const std::vector<wchar_t>& osc_string ) const;

  /* True for OSC strings that the terminal emulator handles itself. */
  static bool is_native( const std::vector<wchar_t>& osc_string );

  /* The policy in effect for this process. Empty unless set. */
  static const OSCPolicy& active( void );
  static void set_active( const OSCPolicy& policy );
};
}

#endif
