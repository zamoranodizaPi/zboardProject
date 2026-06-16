`timescale 1ns / 1ps

module pwm_gen #(
  parameter integer CLK_HZ = 50_000_000,
  parameter integer PWM_HZ = 20_000,
  parameter integer PWM_BITS = 10
) (
  input wire clk,
  input wire rst,
  input wire [PWM_BITS-1:0] duty,
  output reg pwm = 1'b0
);

  localparam integer PERIOD = CLK_HZ / PWM_HZ;
  localparam integer COUNT_W = clog2(PERIOD + 1);

  reg [COUNT_W-1:0] count = {COUNT_W{1'b0}};
  wire [31:0] threshold = (PERIOD * duty) >> PWM_BITS;

  always @(posedge clk) begin
    if (rst) begin
      count <= {COUNT_W{1'b0}};
      pwm <= 1'b0;
    end else begin
      if (count == PERIOD - 1) count <= {COUNT_W{1'b0}};
      else count <= count + {{(COUNT_W-1){1'b0}}, 1'b1};
      pwm <= count < threshold[COUNT_W-1:0];
    end
  end

  function integer clog2;
    input integer value;
    integer i;
    begin
      clog2 = 0;
      for (i = value - 1; i > 0; i = i >> 1) clog2 = clog2 + 1;
    end
  endfunction

endmodule
