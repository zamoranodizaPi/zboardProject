`timescale 1ns/1ps

module sync_generator #(
    parameter integer HALF_CYCLE_TICKS = 1000
) (
    input  wire clk,
    input  wire rst,
    input  wire tick_enable,
    output reg  sync_square,
    output reg  zero_cross_pulse
);
    localparam integer COUNT_WIDTH = $clog2(HALF_CYCLE_TICKS);
    /* verilator lint_off WIDTH */
    localparam [COUNT_WIDTH-1:0] LAST_TICK = HALF_CYCLE_TICKS - 1;
    /* verilator lint_on WIDTH */

    reg [COUNT_WIDTH-1:0] tick_count;

    always @(posedge clk) begin
        if (rst) begin
            tick_count <= {COUNT_WIDTH{1'b0}};
            sync_square <= 1'b0;
            zero_cross_pulse <= 1'b0;
        end else begin
            zero_cross_pulse <= 1'b0;

            if (tick_enable && tick_count == LAST_TICK) begin
                tick_count <= {COUNT_WIDTH{1'b0}};
                sync_square <= ~sync_square;
                zero_cross_pulse <= 1'b1;
            end else if (tick_enable) begin
                tick_count <= tick_count + 1'b1;
            end
        end
    end
endmodule
