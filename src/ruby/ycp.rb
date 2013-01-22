# -----------------------------------------------------------------------\
# |                                                                      |
# |                      __   __    ____ _____ ____                      |
# |                      \ \ / /_ _/ ___|_   _|___ \                     |
# |                       \ V / _` \___ \ | |   __) |                    |
# |                        | | (_| |___) || |  / __/                     |
# |                        |_|\__,_|____/ |_| |_____|                    |
# |                                                                      |
# |                                                                      |
# | ruby language support                              (C) Novell Inc.   |
# \----------------------------------------------------------------------/
#
# Author: Duncan Mac-Vicar <dmacvicar@suse.de>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.
#

# Load the native part (.so)
require 'ycpx'

#--------------------------------------
#
# YCP
#

module YCP
  def self.add_ycp_module(mname)
    #y2internal("trying to add import #{mname}")
    self.import(mname)
    m = Module.new
    self.each_symbol(mname) do |sname,stype|
      if (stype == :function) and !sname.empty?
        m.module_eval <<-"END"
          def self.#{sname}(*args)
            return YCP::call_ycp_function("#{mname}", :#{sname}, *args)
          end
        END
      end # if function
    end
    self.const_set(mname, m)
  end
end

#--------------------------------------
#
# Kernel
#

module Kernel
  alias require_ require 
  def require(name)
    if name =~ /^ycp\/(.+)$/
      ycpns = $1

      begin
        YCP::add_ycp_module(ycpns.upcase)
      rescue RuntimeError => e
        YCP::add_ycp_module(ycpns.capitalize)
      end
      return true
    end
    return require_(name)
  end
end


#--------------------------------------
#
# YCP::Ui
#

module YCP
  module Ui
    #my @e_logging = qw(y2debug y2milestone y2warning y2error y2security y2internal);
    
  # Define symbols for the UI
  ui_terms = [ :BarGraph, :Bottom, :CheckBox, :ColoredLabel, :ComboBox, :Date,
    :DownloadProgress, :DumbTab, :DummySpecialWidget, :Empty, :Frame, :HBox, :HBoxvHCenter,
    :HMultiProgressMeter, :HSpacing, :HSquash, :HStretch, :HVCenter, :HVSquash,
    :HVStretch, :HWeight, :Heading, :IconButton, :Image, :IntField, :Label, :Left, :LogView,
    :MarginBox, :MenuButton, :MinHeight, :MinSize, :MinWidth, :MultiLineEdit,
    :MultiSelectionBox, :PackageSelector, :PatternSelector, :PartitionSplitter,
    :Password, :PkgSpecial, :ProgressBar, :PushButton, :RadioButton,
    :RadioButtonGroup, :ReplacePoint, :RichText, :Right, :SelectionBox, :Slider, :Table,
    :TextEntry, :Time, :Top, :Tree, :VBox, :VCenter, :VMultiProgressMeter, :VSpacing,
    :VSquash, :VStretch, :VWeight, :Wizard,
    :id, :opt ]
       
#     buffer = String.new
#     buffer << "["
#     ui_terms.each do |t|
#       buffer << " :" << t.to_s.downcase << ","
#     end
#     buffer <<  " ]"
#     puts buffer
    # If the method name contains underscores, convert to camel case
#     while method =~ /([^_]*)_(.)(.*)/ 
#            method = $1 + $2.upcase + $3
#        end
         
    # for each symbol define a util function that will create a term
    ui_terms.each do | term_name |
      define_method(term_name) do | *args |
        t = YaST::Term.new(term_name.to_s)
        args.each do |arg|
          t.add(arg)
        end
        return t
      end
      alias_method term_name.to_s.downcase, term_name
    end

  end # end Ui module
end


#--------------------------------------
#
# YaST::TermBuilder
#

module YaST
  class TermBuilder
    # blank slate
    instance_methods.each { |m| undef_method m unless (m =~ /^__|instance_eval$/)}
    
    def initialize(&block)
        @term = nil
        @term = instance_eval(&block)
    end
    
    def method_missing(name, *args, &block )
  #    puts "hi #{name.to_s} | #{args}"
      term = nil
      elements = block ? nil : args
      @__to_s = nil # invalidate to_s cache
      term = YaST::Term.new(name.to_s)
      if not elements.nil?
        elements.each do | e |
          term.add(e)
        end
        return term
      else
        r = instance_eval(&block)
        puts term.class
        term.add(r) if not r.nil?
      end
      return term
    end
    
    def to_s
      return @term.to_s
    end
    
    def term
      return @term
    end
    
  end
end # module YaST

#--------------------------------------
#
# YaST::logger
#

module YaST
  def y2_logger_helper(*args)
    level = args.shift
    
    caller[0] =~ /(.+):(\d+):in `([^']+)'/
    y2_logger(level, "Ruby", $1, $2.to_i, "", args[0])
  end
  
  def y2debug(*args)
    y2_logger_helper(0, args)
  end
  
  def y2milestone(*args)
    y2_logger_helper(1, args)
  end
  
  def y2warning(*args)
    y2_logger_helper(2, args)
  end
  
  def y2error(*args)
    y2_logger_helper(3, args)
  end
  
  def y2security(*args)
    y2_logger_helper(4, args)
  end
  
  def y2internal(*args)
    y2_logger_helper(5, args)
  end
end # module YaST
