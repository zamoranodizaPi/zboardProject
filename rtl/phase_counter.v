`timescale 1ns/1ps

module phase_counter #(
    parameter integer PHASE_MAX = 180
) (
    input  wire clk,
    input  wire rst,
    input  wire zero_cross_pulse,
    input  wire tick_enable,
    output reg  [$clog2(PHASE_MAX + 1)-1:0] phase_count
);
    localparam integer PHASE_WIDTH = $clog2(PHASE_MAX + 1);
    /* verilator lint_off WIDTH */
    localparam [PHASE_WIDTH-1:0] PHASE_LIMIT = PHASE_MAX;
    /* verilator lint_on WIDTH */

    always @(posedge clk) begin
        if (rst) begin
            phase_count <= 0;
        end else if (zero_cross_pulse) begin
            phase_count <= 0;
        end else if (tick_enable && phase_count < PHASE_LIMIT) begin
            phase_count <= phase_count + 1'b1;
        end
    end
endmodule
