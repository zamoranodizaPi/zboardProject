`timescale 1ns/1ps

module tb_top;
    localparam integer CLK_PERIOD_NS = 10;
    localparam integer HALF_CYCLE_TICKS = 181;
    localparam integer PHASE_MAX = 180;
    localparam integer ANGLE_STEP = 15;
    localparam integer ANGLE_DEFAULT = 90;
    localparam integer GATE_PULSE_TICKS = 4;

    reg clk = 1'b0;
    reg rst = 1'b1;
    reg btn_up = 1'b0;
    reg btn_down = 1'b0;
    reg btn_reset = 1'b0;
    reg [$clog2(PHASE_MAX + 1)-1:0] last_angle = ANGLE_DEFAULT;

    wire sync_square;
    wire zero_cross_pulse;
    wire gate_pulse;
    wire [$clog2(PHASE_MAX + 1)-1:0] phase_count;
    wire [$clog2(PHASE_MAX + 1)-1:0] angle_setpoint;

    top #(
        .HALF_CYCLE_TICKS(HALF_CYCLE_TICKS),
        .PHASE_TICK_DIV(1),
        .PHASE_MAX(PHASE_MAX),
        .ANGLE_STEP(ANGLE_STEP),
        .ANGLE_DEFAULT(ANGLE_DEFAULT),
        .GATE_PULSE_TICKS(GATE_PULSE_TICKS)
    ) dut (
        .clk(clk),
        .rst(rst),
        .btn_up(btn_up),
        .btn_down(btn_down),
        .btn_reset(btn_reset),
        .sync_square(sync_square),
        .zero_cross_pulse(zero_cross_pulse),
        .gate_pulse(gate_pulse),
        .phase_count(phase_count),
        .angle_setpoint(angle_setpoint)
    );

    always #(CLK_PERIOD_NS / 2) clk = ~clk;

    task pulse_up;
        begin
            @(negedge clk);
            btn_up = 1'b1;
            repeat (3) @(posedge clk);
            @(negedge clk);
            btn_up = 1'b0;
        end
    endtask

    task pulse_down;
        begin
            @(negedge clk);
            btn_down = 1'b1;
            repeat (3) @(posedge clk);
            @(negedge clk);
            btn_down = 1'b0;
        end
    endtask

    task pulse_reset;
        begin
            @(negedge clk);
            btn_reset = 1'b1;
            repeat (3) @(posedge clk);
            @(negedge clk);
            btn_reset = 1'b0;
        end
    endtask

    initial begin
        $dumpfile("build/waves.vcd");
        $dumpvars(0, tb_top);

        $display("time_ns, event, sync, phase, angle, gate");

        repeat (8) @(posedge clk);
        rst = 1'b0;

        repeat (2 * HALF_CYCLE_TICKS) @(posedge clk);

        pulse_down();
        repeat (2 * HALF_CYCLE_TICKS) @(posedge clk);

        pulse_down();
        repeat (2 * HALF_CYCLE_TICKS) @(posedge clk);

        pulse_up();
        repeat (2 * HALF_CYCLE_TICKS) @(posedge clk);

        pulse_up();
        repeat (2 * HALF_CYCLE_TICKS) @(posedge clk);

        pulse_reset();
        repeat (2 * HALF_CYCLE_TICKS) @(posedge clk);

        $display("Simulation finished. Open build/waves.vcd with GTKWave.");
        $finish;
    end

    always @(posedge clk) begin
        if (!rst) begin
            if (zero_cross_pulse) begin
                $display("%0t, zero_cross, %0b, %0d, %0d, %0b",
                         $time, sync_square, phase_count, angle_setpoint, gate_pulse);
            end

            if (angle_setpoint != last_angle) begin
                $display("%0t, angle_change, %0b, %0d, %0d, %0b",
                         $time, sync_square, phase_count, angle_setpoint, gate_pulse);
                last_angle <= angle_setpoint;
            end

            if (gate_pulse) begin
                $display("%0t, gate_pulse, %0b, %0d, %0d, %0b",
                         $time, sync_square, phase_count, angle_setpoint, gate_pulse);
            end
        end
    end
endmodule
