ROOT_DIR = File.expand_path('../../..',__FILE__)
binary_path = "#{ROOT_DIR}/build/src/binary"
require "fileutils"
if !File.exists? "#{binary_path}/yast"
  FileUtils.ln_s binary_path, "#{binary_path}/yast" #to load builtinx.so
end
$:.unshift binary_path # yastx.so
$:.unshift "#{ROOT_DIR}/src/ruby"       # yast.rb
ENV["Y2DIR"] = File.dirname(__FILE__)

