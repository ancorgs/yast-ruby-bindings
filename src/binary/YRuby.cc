/*---------------------------------------------------------------------\
|                                                                      |
|                      __   __    ____ _____ ____                      |
|                      \ \ / /_ _/ ___|_   _|___ \                     |
|                       \ V / _` \___ \ | |   __) |                    |
|                        | | (_| |___) || |  / __/                     |
|                        |_|\__,_|____/ |_| |_____|                    |
|                                                                      |
|                                                                      |
| ruby language support                              (C) Novell Inc.   |
\----------------------------------------------------------------------/

Author: Duncan Mac-Vicar <dmacvicar@suse.de>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version
2 of the License, or (at your option) any later version.

*/

#include <stdlib.h>
#include <stdarg.h>
#include <list>
#include <iosfwd>
#include <sstream>
#include <iomanip>

// Ruby stuff
#include <ruby.h>
#include <ruby/encoding.h>


#define y2log_component "Y2Ruby"
#include <ycp/y2log.h>
#include <ycp/pathsearch.h>

#include <ycp/YCPVoid.h>


#include "YRuby.h"
#include "Y2RubyUtils.h"

#include "Y2RubyTypeConv.h"
#include "Y2YCPTypeConv.h"

void inject_last_exception_method(VALUE& module,const string& message, const string& module_name)
{
  //doing injection from C++ is quite complex, but we have eval, so we can do it in ruby :)

  string m(message);
  replace(m.begin(), m.end(), '\'', '"');

  string code("module ");
  code += module_name;
  code += "\ndef self.last_exception\n'";
  code += m;
  code += "'\nend\nend";
  rb_eval_string(code.c_str());
}

YRuby * YRuby::_yRuby = 0;
bool YRuby::_y_ruby_finalized = false;

YRuby::YRuby()
{
  y2debug( "Initializing ruby interpreter." );

  RUBY_INIT_STACK;
  ruby_init();
  // call ruby_process_options to invoke prelude.rb which defines Mutex#synchronize
  // see http://www.ruby-forum.com/topic/4408161
  static char* args[] = { "ruby", "/dev/null" };
  ruby_process_options(2, args);
  ruby_script("yast");
  ruby_init_loadpath();

  rb_enc_find_index("encdb");

  VALUE ycp_references = Data_Wrap_Struct(rb_cObject, gc_mark, gc_free, & value_references_from_ycp);
  rb_global_variable(&ycp_references);
}

void YRuby::gc_mark(void *object)
{
  refcount_map_t * vrby = (refcount_map_t *) object;

  y2milestone("mark: map size is %u", vrby->size());
  refcount_map_t::iterator
    b = vrby->begin(),
    e = vrby->end(),
    it;
  for (it = b; it != e; ++it) {
    y2milestone("marking: value %ld refcount %d", it->first, it->second);
    rb_gc_mark(it->first);
  }
}

void YRuby::gc_free(void *object)
{
  refcount_map_t * vrby = (refcount_map_t *) object;

  y2milestone("free: map size is %u", vrby->size());
}

YRuby::~YRuby()
{
    y2milestone( "Shutting down ruby interpreter." );
    ruby_finalize();
    _y_ruby_finalized = true;
}


YRuby *
YRuby::yRuby()
{
  if ( ! _yRuby && !_y_ruby_finalized )
    _yRuby = new YRuby();

  return _yRuby;
}


YCPValue
YRuby::destroy()
{
  if ( _yRuby )
  {
    delete _yRuby;
    _yRuby = 0;
  }

  return YCPVoid();
}

/**
 * Loads a module.
 */
YCPValue
YRuby::loadModule( YCPList argList )
{
  YRuby::yRuby();
  string module_path = argList->value(1)->asString()->value();
  int error = 0;
  rb_protect( (VALUE (*)(VALUE))rb_require, (VALUE) module_path.c_str(), &error);
  if (error)
    return YCPError( "Ruby::loadModule() / Can't load ruby module '" + module_path + "'" );
  return YCPVoid();
}

// rb_protect-enabled rb_funcall, see below
static VALUE
protected_call(VALUE args)
{
  VALUE *values = (VALUE *)args;
  return rb_funcall3(values[0], values[1], (int)values[2], values+3);
}

/**
 * @param argList arguments start 1!, 0 is dummy
 */
YCPValue YRuby::callInner (string module_name, string function,
                  YCPList argList, constTypePtr wanted_result_type)
{
  RUBY_INIT_STACK  // bnc#708059
  VALUE module = y2ruby_nested_const_get(module_name);
  if (module == Qnil)
  {
    y2milestone ("The Ruby module '%s' is not provided by its rb file. Try YCP prefix.", module_name.c_str());
    string alternative_name = string("YCP::")+module_name;
    module = y2ruby_nested_const_get(alternative_name);
    if (module == Qnil)
    {
      y2error ("The Ruby module '%s' is not provided by its rb file", alternative_name.c_str());
      return YCPVoid();
    }
  }

  int size = argList.size();

  // make rooms for arguments to
  // the ruby function
  // +3 for module, function, and number of args
  // to pass to protected_call()
  VALUE values[size+3];
  values[0] = module;
  values[1] = rb_intern(function.c_str());
  values[2] = size;
  for (int i = 0 ; i < size; ++i )
  {
    // get the
    YCPValue v = argList->value(i);
    y2debug("Adding argument %d of type %s", i, v->valuetype_str());
    values[i+3] = ycpvalue_2_rbvalue(v);
  }

  y2debug( "Will call function '%s' in module '%s' with '%d' arguments", function.c_str(), module_name.c_str(), size-1);

  int error;
  VALUE result = rb_protect(protected_call, (VALUE)values, &error);
  if (error)
  {
    VALUE exception = rb_gv_get("$!"); /* get last exception */
    VALUE reason = rb_funcall(exception, rb_intern("message"), 0 );
    VALUE trace = rb_gv_get("$@"); /* get last exception trace */
    VALUE backtrace = rb_ary_join(trace, rb_str_new("\n\t", 2));
    y2error("%s.%s failed:%s\n\t%s", module_name.c_str(), function.c_str(), StringValuePtr(reason),StringValuePtr(backtrace));
    //workaround if last_exception failed, then return always string with message
    if(function == "last_exception") //TODO constantify last_exception
    {
      return YCPString(StringValuePtr(reason));
    }
    inject_last_exception_method(module,StringValuePtr(reason),module_name);
    return YCPVoid();
  }
  else
  {
    y2debug( "Called function '%s' in module '%s'", function.c_str(), module_name.c_str());
  }
  return rbvalue_2_ycpvalue(result);
}

YCPValue YRuby::callClient(const string& path)
{
  int error;
  rb_protect( (VALUE (*)(VALUE))rb_require, (VALUE) "ycp", &error);
  if (error)
  {
    y2error ("cannot require ycp");
    return YCPVoid();
  }

  VALUE wfm_module = y2ruby_nested_const_get("YCP::WFM");
  VALUE result = rb_funcall(wfm_module, rb_intern("run_client"), 1, rb_str_new2(path.c_str()));
  return rbvalue_2_ycpvalue(result);
}

