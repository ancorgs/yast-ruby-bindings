ROOT_DIR = File.expand_path('../../..', __FILE__)
binary_path = "#{ROOT_DIR}/build/src/binary"
require "fileutils"
if !File.exists? "#{binary_path}/yast"
  FileUtils.ln_s binary_path, "#{binary_path}/yast" # to load builtinx.so
end
if !File.exists? "#{binary_path}/plugin"
  FileUtils.ln_s binary_path, "#{binary_path}/plugin" # to load py2lang_ruby.so for calling testing ruby clients
end
$:.unshift binary_path # yastx.so
$:.unshift "#{ROOT_DIR}/src/ruby"       # yast.rb
ENV["Y2DIR"] = binary_path + ":" + File.dirname(__FILE__) + "/test_module"
