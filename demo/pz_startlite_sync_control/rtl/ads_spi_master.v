`timescale 1ns / 1ps

module ads_spi_master #(
  parameter integer CLK_HZ = 50_000_000,
  parameter integer SPI_HZ = 2_000_000,
  parameter integer FRAME_WORDS = 9
) (
  input  wire clk,
  input  wire rst,
  input  wire drdy_n,
  output reg  cs_n = 1'b1,
  output reg  sclk = 1'b0,
  output wire mosi,
  input  wire miso,
  output reg  frame_valid = 1'b0,
  output reg  frame_bad = 1'b0,
  output reg [15:0] status = 16'd0,
  output reg signed [15:0] ch0 = 16'sd0,
  output reg signed [15:0] ch1 = 16'sd0,
  output reg signed [15:0] ch2 = 16'sd0,
  output reg signed [15:0] ch3 = 16'sd0,
  output reg signed [15:0] ch4 = 16'sd0,
  output reg signed [15:0] ch5 = 16'sd0,
  output reg signed [15:0] ch6 = 16'sd0,
  output reg signed [15:0] ch7 = 16'sd0,
  output reg [31:0] good_frames = 32'd0,
  output reg [31:0] bad_frames = 32'd0
);

  localparam integer FRAME_BITS = FRAME_WORDS * 16;
  localparam integer DIVIDER = CLK_HZ / (SPI_HZ * 2);
  localparam integer DIVIDER_W = clog2(DIVIDER + 1);

  localparam [1:0] ST_IDLE = 2'd0;
  localparam [1:0] ST_ASSERT = 2'd1;
  localparam [1:0] ST_SHIFT = 2'd2;
  localparam [1:0] ST_DONE = 2'd3;

  reg [1:0] state = ST_IDLE;
  reg [DIVIDER_W-1:0] clk_div = {DIVIDER_W{1'b0}};
  reg [8:0] bit_count = 9'd0;
  reg [3:0] word_index = 4'd0;
  reg [15:0] word_shift = 16'd0;
  reg drdy_meta = 1'b1;
  reg drdy_sync = 1'b1;
  reg drdy_prev = 1'b1;
  reg drdy_armed = 1'b1;

  wire drdy_fall = drdy_prev & ~drdy_sync;
  wire status_ok = status[15:12] == 4'h5 || status == 16'hA751;

  assign mosi = 1'b0;

  always @(posedge clk) begin
    drdy_meta <= drdy_n;
    drdy_sync <= drdy_meta;
    drdy_prev <= drdy_sync;
  end

  always @(posedge clk) begin
    frame_valid <= 1'b0;
    frame_bad <= 1'b0;

    if (rst) begin
      state <= ST_IDLE;
      cs_n <= 1'b1;
      sclk <= 1'b0;
      clk_div <= {DIVIDER_W{1'b0}};
      bit_count <= 9'd0;
      word_index <= 4'd0;
      word_shift <= 16'd0;
      status <= 16'd0;
      good_frames <= 32'd0;
      bad_frames <= 32'd0;
      drdy_armed <= 1'b1;
    end else begin
      case (state)
        ST_IDLE: begin
          cs_n <= 1'b1;
          sclk <= 1'b0;
          clk_div <= {DIVIDER_W{1'b0}};
          bit_count <= 9'd0;
          word_index <= 4'd0;
          word_shift <= 16'd0;
          if (drdy_sync) drdy_armed <= 1'b1;
          if ((drdy_fall || !drdy_sync) && drdy_armed) begin
            drdy_armed <= 1'b0;
            state <= ST_ASSERT;
          end
        end

        ST_ASSERT: begin
          cs_n <= 1'b0;
          sclk <= 1'b0;
          state <= ST_SHIFT;
        end

        ST_SHIFT: begin
          if (clk_div == DIVIDER - 1) begin
            clk_div <= {DIVIDER_W{1'b0}};
            sclk <= ~sclk;
            if (!sclk) begin
              word_shift <= {word_shift[14:0], miso};
              bit_count <= bit_count + 9'd1;
              if (bit_count[3:0] == 4'd15) begin
                capture_word(word_index, {word_shift[14:0], miso});
                word_index <= word_index + 4'd1;
              end
              if (bit_count == FRAME_BITS - 1) state <= ST_DONE;
            end
          end else begin
            clk_div <= clk_div + {{(DIVIDER_W-1){1'b0}}, 1'b1};
          end
        end

        ST_DONE: begin
          cs_n <= 1'b1;
          sclk <= 1'b0;
          if (status_ok) begin
            frame_valid <= 1'b1;
            good_frames <= good_frames + 32'd1;
          end else begin
            frame_bad <= 1'b1;
            bad_frames <= bad_frames + 32'd1;
          end
          state <= ST_IDLE;
        end
      endcase
    end
  end

  task capture_word;
    input [3:0] idx;
    input [15:0] value;
    begin
      case (idx)
        4'd0: status <= value;
        4'd1: ch0 <= value;
        4'd2: ch1 <= value;
        4'd3: ch2 <= value;
        4'd4: ch3 <= value;
        4'd5: ch4 <= value;
        4'd6: ch5 <= value;
        4'd7: ch6 <= value;
        4'd8: ch7 <= value;
        default: begin end
      endcase
    end
  endtask

  function integer clog2;
    input integer value;
    integer i;
    begin
      clog2 = 0;
      for (i = value - 1; i > 0; i = i >> 1) clog2 = clog2 + 1;
    end
  endfunction

endmodule
