$LOAD_PATH << File.dirname(__FILE__)
require "test_helper"

require "ycp/path"

class PathTest < YCP::TestCase
  def test_initialize
    assert_equal ".etc", YCP::Path.new(".etc").value
    assert_equal '."et?c"', YCP::Path.new('."et?c"').value
  end

  def test_load_from_string
    assert_equal ".etc", YCP::Path.load_from_string("etc").value
    assert_equal '."et?c"', YCP::Path.load_from_string('et?c').value
  end

  def test_add
    etc = YCP::Path.new '.etc'
    sysconfig = YCP::Path.new '.sysconfig'
    assert_equal ".etc.sysconfig", (etc + sysconfig).value
    assert_equal ".etc.sysconfig", (etc + 'sysconfig').value
  end

  def test_equals
    assert_equal YCP::Path.new(".\"\xff\""), YCP::Path.new(".\"\xFF\"")
    assert_equal YCP::Path.new(".\"\x41\""), YCP::Path.new(".\"A\"")
    assert_not_equal YCP::Path.new(".\"\""), YCP::Path.new('.')
  end
end
