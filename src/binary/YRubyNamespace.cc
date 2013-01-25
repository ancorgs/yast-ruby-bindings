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

#include "YRubyNamespace.h"

// Ruby stuff
#include <ruby.h>

#define y2log_component "Y2Ruby"
#include <ycp/y2log.h>

#include <ycp/YCPElement.h>
#include <ycp/Type.h>
#include <ycp/YCPVoid.h>
//#include <YCP.h>
#include <stdio.h>

#include "YRuby.h"
#include "Y2RubyUtils.h"

/**
 * The definition of a function that is implemented in Ruby
 */
class Y2RubyFunctionCall : public Y2Function
{
  //! module name
  string m_module_name;
  //! function name, excluding module name
  string m_local_name;
  //! function type
  constFunctionTypePtr m_type;
  //! data prepared for the inner call
  YCPList m_call;

public:
  Y2RubyFunctionCall (const string &module_name,
                      const string &local_name,
                      constFunctionTypePtr function_type
                     ) :
      m_module_name (module_name),
      m_local_name (local_name),
      m_type (function_type)
  {
    // placeholder, formerly function name
    m_call->add (YCPVoid ());
  }

  //! if true, the ruby function is passed the module name
  virtual bool isMethod () = 0;

  //! called by YEFunction::evaluate
  virtual YCPValue evaluateCall ()
  {
    return YRuby::yRuby()->callInner ( m_module_name,
                                       m_local_name,
                                       isMethod (),
                                       m_call,
                                       m_type->returnType() );
  }
  /**
  * Attaches a parameter to a given position to the call.
  * @return false if there was a type mismatch
  */
  virtual bool attachParameter (const YCPValue& arg, const int position)
  {
    m_call->set (position+1, arg);
    return true;
  }

  /**
   * What type is expected for the next appendParameter (val) ?
   * (Used when calling from Ruby, to be able to convert from the
   * simple type system of Ruby to the elaborate type system of YCP)
   * @return Type::Any if number of parameters exceeded
   */
  virtual constTypePtr wantedParameterType () const
  {
    // -1 for the function name
    int params_so_far = m_call->size ()-1;
    return m_type->parameterType (params_so_far);
  }

  /**
   * Appends a parameter to the call.
   * @return false if there was a type mismatch
   */
  virtual bool appendParameter (const YCPValue& arg)
  {
    y2internal("Adding parameter to function %s::%s of type %s", m_module_name.c_str(), m_local_name.c_str(), arg->valuetype_str());
    m_call->add (arg);
    return true;
  }

  /**
   * Signal that we're done adding parameters.
   * @return false if there was a parameter missing
   */
  virtual bool finishParameters ()
  {
    return true;
  }


  virtual bool reset ()
  {
    m_call = YCPList ();
    // placeholder, formerly function name
    m_call->add (YCPVoid ());
    return true;
  }

  /**
   * Something for remote namespaces
   */
  virtual string name () const
  {
    return m_local_name;
  }
};

class Y2RubySubCall : public Y2RubyFunctionCall
{
public:
  Y2RubySubCall (const string &module_name,
                 const string &local_name,
                 constFunctionTypePtr function_type
                ) :
      Y2RubyFunctionCall (module_name, local_name, function_type)
  {}
  virtual bool isMethod ()
  {
    return false;
  }
};

class Y2RubyMethodCall : public Y2RubyFunctionCall
{
public:
  Y2RubyMethodCall (const string &module_name,
                    const string &local_name,
                    constFunctionTypePtr function_type
                   ) :
      Y2RubyFunctionCall (module_name, local_name, function_type)
  {}
  virtual bool isMethod ()
  {
    return true;
  }
};

//class that allow us to simulate variable and in fact call ruby accessors
class VariableSymbolEntry : public SymbolEntry
{
private:
  const string &module_name;
public:
  //not so nice constructor that allow us to hook to symbol entry variable reading
  VariableSymbolEntry(const string &r_module_name, const Y2Namespace* name_space, unsigned int position, const char *name, constTypePtr type) :
   SymbolEntry(name_space, position, name, SymbolEntry::c_variable, type),module_name(r_module_name)    {}

  YCPValue setValue (YCPValue value)
  {
    YCPList l;
    //tricky first value is void TODO check it
    YCPVoid v;
    l.add(v);
    l.add(value);
    string method_name = name();
    method_name += "=";
    return YRuby::yRuby()->callInner ( module_name,
                                       method_name,
                                       true,
                                       l,
                                       type());
  }

  YCPValue value () const
  {
    YCPList l;
    //tricky first value is void TODO check it
    YCPVoid v;
    l.add(v);
    return YRuby::yRuby()->callInner ( module_name,
                                       name(),
                                       true,
                                       l,
                                       type());
  }

};

class Y2Ruby;

YRubyNamespace::YRubyNamespace (string name)
    : m_name (name),
    m_all_methods (true)
{
  y2milestone("Creating namespace for '%s'", name.c_str());
  VALUE module = y2ruby_nested_const_get(name);
  if (module == Qnil)
  {
    y2milestone ("The Ruby module '%s' is not provided by its rb file. Try it with YCP prefix.", name.c_str());
    //modules can live in YCP namespace, it is OK
    name = string("YCP::")+name;
    module = y2ruby_nested_const_get(name);
    if (module == Qnil)
    {
      y2error ("The Ruby module '%s' is not provided by its rb file", name.c_str());
      return;
    }
  }
  y2milestone("The module '%s' was found", name.c_str());

  long i = 0; //trace number of added method, so we can add extra one at the end
  //detect if module use new approach for exporting methods or old one
  if (rb_respond_to(module, rb_intern("published_methods" )))
  {
    VALUE methods = rb_funcall(module, rb_intern("published_methods"),0);
    methods = rb_funcall(methods,rb_intern("values"),0);
    for (i = 0; i < RARRAY_LEN(methods); ++i)
    {
      VALUE method = rb_ary_entry(methods,i);
      VALUE method_name = rb_funcall(method, rb_intern("method_name"), 0);
      VALUE type = rb_funcall(method,rb_intern("type"),0);
      string signature = StringValueCStr(type);
      constTypePtr sym_tp = Type::fromSignature(signature);

      constFunctionTypePtr fun_tp = (constFunctionTypePtr) sym_tp;

      // symbol entry for the function
      SymbolEntry *fun_se = new SymbolEntry ( this,
                                              i,// position. arbitrary numbering. must stay consistent when?
                                              RSTRING_PTR(method_name), // passed to Ustring, no need to strdup
                                              SymbolEntry::c_function,
                                              sym_tp);
      fun_se->setGlobal (true);
      // enter it to the symbol table
      enterSymbol (fun_se, 0);
      y2milestone("method: '%s' added", RSTRING_PTR(method_name));
    }
    VALUE variables = rb_funcall(module, rb_intern("published_variables"),0);
    variables = rb_funcall(variables,rb_intern("values"),0);
    int j;
    for (j = 0; j < RARRAY_LEN(variables); ++j)
    {
      VALUE variable = rb_ary_entry(variables,j);
      VALUE variable_name = rb_funcall(variable, rb_intern("variable"), 0);
      VALUE type = rb_funcall(variable,rb_intern("type"),0);
      string signature = StringValueCStr(type);
      constTypePtr sym_tp = Type::fromSignature(signature);

      // symbol entry for the function
      SymbolEntry *se = new VariableSymbolEntry ( name, this,
                                              i+j,// position. arbitrary numbering. must stay consistent when?
                                              rb_id2name(SYM2ID(variable_name)),
                                              sym_tp);
      se->setGlobal (true);
      // enter it to the symbol table
      enterSymbol (se, 0);
      y2milestone("variable: '%s' added", rb_id2name(SYM2ID(variable_name)));
    }
    i += j;
  }
  else
  {
    // we will perform operator- to determine the module methods
    VALUE moduleklassmethods = rb_funcall( rb_cModule, rb_intern("methods"), 0);
    VALUE mymodulemethods = rb_funcall( module, rb_intern("methods"), 0);
    VALUE methods = rb_funcall( mymodulemethods, rb_intern("-"), 1, moduleklassmethods );

    if (methods == Qnil)
    {
      y2error ("Can't see methods in module '%s'", name.c_str());
      return;
    }

    for(i = 0; i < RARRAY_LEN(methods); i++)
    {
      VALUE current = rb_funcall( methods, rb_intern("at"), 1, rb_fix_new(i) );
      if (rb_type(current) == RUBY_T_SYMBOL) {
    current = rb_funcall( current, rb_intern("to_s"), 0);
      }
      y2milestone("New method: '%s'", RSTRING_PTR(current));

      // figure out arity.
      Check_Type(module,T_MODULE);
      VALUE methodobj = rb_funcall( module, rb_intern("method"), 1, current );
      if ( methodobj == Qnil )
      {
        y2error ("Cannot access method object '%s'", RSTRING_PTR(current));
        continue;
      }
      string signature = "any( ";
      VALUE rbarity = rb_funcall( methodobj, rb_intern("arity"), 0);
      int arity = NUM2INT(rbarity);
      for ( int k=0; k < arity; ++k )
      {
        signature += "any";
        if ( k < (arity - 1) )
            signature += ",";
      }
      signature += ")";
      y2internal("going to parse signature: '%s'", signature.c_str());
      constTypePtr sym_tp = Type::fromSignature(signature);

      constFunctionTypePtr fun_tp = (constFunctionTypePtr) sym_tp;

      // symbol entry for the function
      SymbolEntry *fun_se = new SymbolEntry ( this,
                                              i,// position. arbitrary numbering. must stay consistent when?
                                              RSTRING_PTR(current), // passed to Ustring, no need to strdup
                                              SymbolEntry::c_function,
                                              sym_tp);
      fun_se->setGlobal (true);
      // enter it to the symbol table
      enterSymbol (fun_se, 0);
      y2milestone("method: '%s' added", RSTRING_PTR(current));
    }
  }
  //add to all modules method last_exception to get last exception raised inside module
  constTypePtr sym_tp = Type::fromSignature("any()");
  // symbol entry for the function
  SymbolEntry *fun_se = new SymbolEntry ( this,
                                          i,// position. arbitrary numbering.
                                          "last_exception", // passed to Ustring, no need to strdup
                                          SymbolEntry::c_function,
                                          sym_tp);
  fun_se->setGlobal (true);
  // enter it to the symbol table
  enterSymbol (fun_se, 0);
  y2milestone("method: 'last_exception' added");
  y2milestone("%s", symbolsToString().c_str());
}

YRubyNamespace::~YRubyNamespace ()
{}

const string YRubyNamespace::filename () const
{
  // TODO improve
  return ".../" + m_name;
}

// this is for error reporting only?
string YRubyNamespace::toString () const
{
  y2error ("TODO");
  return "{\n"
         "/* this namespace is provided in Ruby */\n"
         "}\n";
}

// called when running and the import statement is encountered
// does initialization of variables
// constructor is handled separately after this
YCPValue YRubyNamespace::evaluate (bool cse)
{
  // so we don't need to do anything
  y2debug ("Doing nothing");
  return YCPNull ();
}

// It seems that this is the standard implementation. why would we
// ever want it to be different?
Y2Function* YRubyNamespace::createFunctionCall (const string name, constFunctionTypePtr required_type)
{
  y2debug ("Creating function call for %s", name.c_str ());
  TableEntry *func_te = table ()->find (name.c_str (), SymbolEntry::c_function);
  if (func_te)
  {
    constTypePtr t = required_type ? required_type : (constFunctionTypePtr)func_te->sentry()->type ();
    if (m_all_methods)
    {
      return new Y2RubyMethodCall (m_name, name, t);
    }
    else
    {
      return new Y2RubySubCall (m_name, name, t);
    }
  }
  y2error ("No such function %s", name.c_str ());
  return NULL;
}
