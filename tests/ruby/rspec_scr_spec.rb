#!/usr/bin/env rspec

require_relative "test_helper"

require "yast/rspec"

describe Yast::RSpec::SCR do
  let(:chroot) { File.join(__FILE__, "chroot") }
  
  def root_content
    Yast::SCR.Read(path(".target.dir"), "/")
  end

  describe "#change_scr_root" do
    it "blah" do
      expect(root_content).not_to eq(["only_one_file"])

      change_scr_root(chroot) do
        expect(root_content).to eq(["only_one_file"])
      end

      expect(root_content).not_to eq(["only_one_file"])
    end
  end
end
