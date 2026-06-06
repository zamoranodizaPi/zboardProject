`timescale 1ns/1ps

module phase_counter #(
    parameter integer PHASE_MAX = 180
) (
    input  wire clk,
    input  wire rst,
    input  wire zero_cross_pulse,
    output reg  [$clog2(PHASE_MAX + 1)-1:0] phase_count
);
    localparam integer PHASE_WIDTH = $clog2(PHASE_MAX + 1);
    localparam [PHASE_WIDTH-1:0] PHASE_LIMIT = PHASE_WIDTH'(PHASE_MAX);

    always @(posedge clk) begin
        if (rst) begin
            phase_count <= 0;
        end else if (zero_cross_pulse) begin
            phase_count <= 0;
        end else if (phase_count < PHASE_LIMIT) begin
            phase_count <= phase_count + 1'b1;
        end
    end
endmodule
