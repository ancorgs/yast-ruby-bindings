#!/usr/bin/env rspec

require_relative "test_helper"

require "yast/y2logger"

module Yast
  describe Y2Logger do
    TEST_MESSAGE = "Testing"

    before do
      @test_logger = Y2Logger.instance
    end

    it "logs debug messages via y2debug()" do
      expect(Yast).to receive(:y2debug).with(Y2Logger::CALL_FRAME, TEST_MESSAGE)
      @test_logger.debug TEST_MESSAGE
    end

    it "logs info messages via y2milestone()" do
      expect(Yast).to receive(:y2milestone).with(Y2Logger::CALL_FRAME, TEST_MESSAGE)
      @test_logger.info TEST_MESSAGE
    end

    it "logs warnings via y2warning()" do
      expect(Yast).to receive(:y2warning).with(Y2Logger::CALL_FRAME, TEST_MESSAGE)
      @test_logger.warn TEST_MESSAGE
    end

    it "logs errors via y2error()" do
      expect(Yast).to receive(:y2error).with(Y2Logger::CALL_FRAME, TEST_MESSAGE)
      @test_logger.error TEST_MESSAGE
    end

    it "logs fatal errors via y2error()" do
      expect(Yast).to receive(:y2error).with(Y2Logger::CALL_FRAME, TEST_MESSAGE)
      @test_logger.fatal TEST_MESSAGE
    end

    it "handles a message passed via block" do
      expect(Yast).to receive(:y2milestone).with(Y2Logger::CALL_FRAME, TEST_MESSAGE)
      @test_logger.info { TEST_MESSAGE }
    end

    it "does not crash when logging an invalid UTF-8 string" do
      # do not process this string otherwise you'll get an exception :-)
      invalid_utf8 = "invalid sequence: \xE3\x80"
      # just make sure it is really an UTF-8 string
      expect(invalid_utf8.encoding).to eq(Encoding::UTF_8)
      expect { Yast.y2milestone(invalid_utf8) }.not_to raise_error
    end

    it "does not crash when logging ASCII string with invalid UTF-8" do
      # do not process this string otherwise you'll get an exception :-)
      invalid_ascii = "invalid sequence: \xE3\x80"
      invalid_ascii.force_encoding(Encoding::ASCII)
      expect { Yast.y2milestone(invalid_ascii) }.not_to raise_error
    end
  end

  describe Yast::Logger do
    it "module adds log() method for accessing the Logger" do
      class Test
        include Yast::Logger
      end
      expect(Test.log).to be_kind_of(::Logger)
      expect(Test.new.log).to be_kind_of(::Logger)
    end
  end
end
