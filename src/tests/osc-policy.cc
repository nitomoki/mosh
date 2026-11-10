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

/* Tests OSC policies: parsing, and carrying OSC strings from application
   output on the server, through diffs, to the client's terminal. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/statesync/completeterminal.h"
#include "src/terminal/oscpolicy.h"
#include "src/terminal/terminaldisplay.h"

using Terminal::OSCPolicy;

static int failures = 0;

static void check( bool ok, const std::string& what )
{
  if ( !ok ) {
    fprintf( stderr, "FAIL: %s\n", what.c_str() );
    failures++;
  }
}

static size_t count( const std::string& haystack, const std::string& needle )
{
  size_t n = 0;
  for ( size_t pos = haystack.find( needle ); pos != std::string::npos; pos = haystack.find( needle, pos + 1 ) ) {
    n++;
  }
  return n;
}

static OSCPolicy policy_from( const std::string& text )
{
  std::vector<std::string> errors;
  OSCPolicy policy = OSCPolicy::parse( text, errors );
  check( errors.empty(), "policy parses without errors: " + text );
  return policy;
}

/* A server and a client connected without a network. The server keeps a copy
   of the state the client acknowledged, as the transport does. */
class Session
{
public:
  OSCPolicy server_policy, client_policy;
  Terminal::Complete server, acked, client;
  Terminal::Framebuffer shown;
  Terminal::Display local;

  Session( const OSCPolicy& s_server_policy, const OSCPolicy& s_client_policy )
    : server_policy( s_server_policy ), client_policy( s_client_policy ), server( 80, 24 ), acked( 80, 24 ),
      client( 80, 24 ), shown( 80, 24 ), local( false )
  {}

  void output( const std::string& s )
  {
    OSCPolicy::set_active( server_policy );
    server.act( s );
  }

  /* Delivers the server state to the client; returns what the client writes
     to its terminal. */
  std::string sync( void )
  {
    OSCPolicy::set_active( server_policy );
    const std::string diff = server.diff_from( acked );
    acked = server;
    return receive( diff, client );
  }

  std::string receive( const std::string& diff, Terminal::Complete& state )
  {
    OSCPolicy::set_active( client_policy );
    state.apply_string( diff );
    const std::string out = local.new_frame( true, shown, state.get_fb() );
    shown = state.get_fb();
    return out;
  }
};

static const std::string USERVAR_A = "\033]1337;SetUserVar=A=eA==\007";
static const std::string USERVAR_A2 = "\033]1337;SetUserVar=A=eQ==\007";
static const std::string META = "\033]5379;";

static void test_parse( void )
{
  std::vector<std::string> errors;
  OSCPolicy policy = OSCPolicy::parse( "# comment\n"
                                       "\n"
                                       "1337 SetUserVar=A= always\n"
                                       "  7 * latest 4096  \n"
                                       "52 ; drop\n",
                                       errors );
  check( errors.empty(), "valid policy has no errors" );
  check( policy.size() == 3, "valid policy has three rules" );
  check( policy.rule( 0 ).mode == OSCPolicy::Mode::Always, "mode always" );
  check( policy.rule( 0 ).max_length == OSCPolicy::DEFAULT_MAX_LENGTH, "default MAXLEN" );
  check( policy.rule( 1 ).prefix.empty() && policy.rule( 1 ).max_length == 4096, "* prefix and MAXLEN" );
  check( policy.rule( 2 ).mode == OSCPolicy::Mode::Drop, "mode drop" );

  const char* invalid[] = { "abc * drop",
                            "052 * drop",
                            "0 * drop",
                            "2 * drop",
                            "8 * drop",
                            "5379 * drop",
                            "1337 * sometimes",
                            "1337 * drop 0",
                            "1337 * drop 99999999",
                            "1337 *",
                            "1337 * drop 10 extra" };
  for ( const char* line : invalid ) {
    errors.clear();
    policy = OSCPolicy::parse( line, errors );
    check( errors.size() == 1 && policy.empty(), std::string( "invalid rule is rejected: " ) + line );
  }

  std::string many;
  for ( size_t i = 0; i < OSCPolicy::MAX_RULES + 1; i++ ) {
    many += "1337 * drop\n";
  }
  errors.clear();
  policy = OSCPolicy::parse( many, errors );
  check( policy.size() == OSCPolicy::MAX_RULES && errors.size() == 1, "rules beyond the limit are rejected" );

  std::string text;
  check( OSCPolicy::decode_hex( OSCPolicy::encode_hex( "1337 * drop\n" ), text ) && text == "1337 * drop\n",
         "hex round trip" );
  check( !OSCPolicy::decode_hex( "abc", text ), "odd-length hex is rejected" );
  check( !OSCPolicy::decode_hex( "zz", text ), "non-hex is rejected" );
}

static void test_without_policy( void )
{
  Session s { OSCPolicy(), OSCPolicy() };
  s.output( USERVAR_A );
  const std::string out = s.sync();
  check( count( out, "1337" ) == 0, "without a policy, unhandled OSC strings are dropped" );
  check( count( out, "5379" ) == 0, "without a policy, no rule announcements are sent" );
}

static void test_latest( void )
{
  const OSCPolicy policy = policy_from( "1337 SetUserVar= latest\n" );
  Session s( policy, policy );
  s.output( USERVAR_A );
  check( count( s.sync(), USERVAR_A ) == 1, "latest: first value is forwarded" );
  s.output( USERVAR_A );
  check( count( s.sync(), "1337" ) == 0, "latest: repeated value is not forwarded" );
  s.output( USERVAR_A2 );
  check( count( s.sync(), USERVAR_A2 ) == 1, "latest: changed value is forwarded" );
}

static void test_always( void )
{
  const OSCPolicy policy = policy_from( "1337 SetUserVar= always\n" );
  Session s( policy, policy );
  s.output( USERVAR_A );
  check( count( s.sync(), USERVAR_A ) == 1, "always: first value is forwarded" );
  s.output( USERVAR_A );
  check( count( s.sync(), USERVAR_A ) == 1, "always: repeated value is forwarded again" );
  s.output( USERVAR_A2 + USERVAR_A );
  const std::string out = s.sync();
  check( count( out, "1337" ) == 1 && count( out, USERVAR_A ) == 1,
         "always: several values between syncs arrive as the last one" );
}

static void test_rules( void )
{
  const OSCPolicy policy = policy_from( "1337 SetUserVar=SECRET= drop\n"
                                        "1337 SetUserVar= always 30\n" );
  Session s( policy, policy );
  s.output( "\033]1337;SetUserVar=SECRET=eA==\007" );
  check( count( s.sync(), "1337" ) == 0, "first matching rule wins (drop)" );
  s.output( "\033]1337;SetUserVar=LONGER_THAN_THIRTY=eA==\007" );
  check( count( s.sync(), "1337" ) == 0, "strings longer than MAXLEN are dropped" );
  s.output( "\033]7;file://host/tmp\007" );
  check( count( s.sync(), "]7;" ) == 0, "unmatched OSC strings are dropped" );
  s.output( USERVAR_A );
  const std::string out = s.sync();
  check( count( out, USERVAR_A ) == 1, "matching string is forwarded" );
  check( count( out, META ) == 0, "rule announcements never reach the terminal" );
}

static void test_native_osc( void )
{
  const OSCPolicy policy = policy_from( "52 ; latest\n" );
  Session s( policy, policy );
  s.output( "\033]0;hello\007\033]52;c;aGk=\007" );
  const std::string out = s.sync();
  check( count( out, "\033]0;hello\007" ) == 1, "titles are unaffected by a policy" );
  check( count( out, "\033]52;c;aGk=\007" ) == 1, "OSC 52 with the c selection is unaffected by a policy" );
  s.output( "\033]52;;aGk=\007" );
  check( count( s.sync(), "\033]52;;aGk=\007" ) == 1, "a rule can carry OSC 52 without a selection" );
}

static void test_injected_announcement( void )
{
  const OSCPolicy policy = policy_from( "1337 SetUserVar= always\n" );
  Session s( policy, policy );
  s.output( "\033]5379;0;99\007" + USERVAR_A );
  const std::string out = s.sync();
  check( count( out, META ) == 0, "announcements in application output are ignored" );
  check( count( out, USERVAR_A ) == 1, "string after an injected announcement is handled normally" );
  const Terminal::Framebuffer::osc_slots_type& slots = s.client.get_fb().get_osc_slots();
  check( !slots.empty() && slots[0].count == 1, "injected announcement does not change the count" );
}

static void test_unsupported_client( void )
{
  const OSCPolicy policy = policy_from( "1337 SetUserVar= always\n" );
  Session s( policy, OSCPolicy() );
  s.output( "text before" + USERVAR_A + "text after" );
  const std::string out = s.sync();
  check( count( out, "1337" ) == 0 && count( out, "5379" ) == 0,
         "client without a policy ignores the strings and announcements" );
  check( !s.server.compare( s.client ), "client without a policy shows the same screen" );
}

static void test_unsupported_server( void )
{
  const OSCPolicy policy = policy_from( "1337 SetUserVar= always\n" );
  Session s( OSCPolicy(), policy );
  s.output( "text" + USERVAR_A );
  const std::string out = s.sync();
  check( count( out, "1337" ) == 0, "server without a policy forwards nothing extra" );
  check( !s.server.compare( s.client ), "server without a policy shows the same screen" );
}

/* The transport may build a diff from an older acknowledged state than the
   one the client last drew, when acknowledgements are lost. */
static void test_diff_from_older_state( void )
{
  const OSCPolicy policy = policy_from( "1337 SetUserVar= always\n" );
  Session s( policy, policy );
  const Terminal::Complete base = s.acked;

  s.output( USERVAR_A );
  check( count( s.sync(), USERVAR_A ) == 1, "value is forwarded once" );

  s.output( "more text" );
  OSCPolicy::set_active( policy );
  Terminal::Complete from_base = base;
  std::string out = s.receive( s.server.diff_from( base ), from_base );
  check( count( out, "1337" ) == 0, "resent diff does not repeat a value already shown" );

  s.output( USERVAR_A );
  OSCPolicy::set_active( policy );
  from_base = base;
  out = s.receive( s.server.diff_from( base ), from_base );
  check( count( out, USERVAR_A ) == 1, "resent diff still delivers a new repetition" );
}

int main()
{
  test_parse();
  test_without_policy();
  test_latest();
  test_always();
  test_rules();
  test_native_osc();
  test_injected_announcement();
  test_unsupported_client();
  test_unsupported_server();
  test_diff_from_older_state();

  if ( failures > 0 ) {
    fprintf( stderr, "%d check(s) failed\n", failures );
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
