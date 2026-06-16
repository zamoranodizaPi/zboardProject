`timescale 1ns / 1ps

module zero_cross_freq #(
  parameter integer SAMPLE_RATE_HZ = 8000,
  parameter integer MIN_PERIOD_SAMPLES = 80,
  parameter integer MAX_PERIOD_SAMPLES = 220
) (
  input wire clk,
  input wire rst,
  input wire sample_valid,
  input wire signed [15:0] sample,
  output reg [31:0] freq_mhz = 32'd60000,
  output reg locked = 1'b0,
  output reg zero_cross_pulse = 1'b0
);

  reg signed [15:0] prev = 16'sd0;
  reg [15:0] period_count = 16'd0;
  reg [7:0] lock_score = 8'd0;

  always @(posedge clk) begin
    zero_cross_pulse <= 1'b0;
    if (rst) begin
      prev <= 16'sd0;
      period_count <= 16'd0;
      freq_mhz <= 32'd60000;
      locked <= 1'b0;
      lock_score <= 8'd0;
    end else if (sample_valid) begin
      if (period_count != 16'hffff) period_count <= period_count + 16'd1;

      if (prev < 0 && sample >= 0) begin
        zero_cross_pulse <= 1'b1;
        if (period_count >= MIN_PERIOD_SAMPLES && period_count <= MAX_PERIOD_SAMPLES) begin
          if (period_count < 16'd129) freq_mhz <= 32'd62000;
          else if (period_count > 16'd138) freq_mhz <= 32'd58000;
          else freq_mhz <= 32'd60000;
          if (lock_score < 8'd200) lock_score <= lock_score + 8'd8;
        end else begin
          if (lock_score > 8'd8) lock_score <= lock_score - 8'd8;
          else lock_score <= 8'd0;
        end
        period_count <= 16'd0;
      end

      locked <= lock_score > 8'd80;
      prev <= sample;
    end
  end

endmodule
