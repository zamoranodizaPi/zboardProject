`timescale 1ns / 1ps

module sync_control_axi_regs #(
  parameter integer C_S_AXI_DATA_WIDTH = 32,
  parameter integer C_S_AXI_ADDR_WIDTH = 8
) (
  input wire s_axi_aclk,
  input wire s_axi_aresetn,

  input wire [C_S_AXI_ADDR_WIDTH-1:0] s_axi_awaddr,
  input wire s_axi_awvalid,
  output reg s_axi_awready,
  input wire [C_S_AXI_DATA_WIDTH-1:0] s_axi_wdata,
  input wire [(C_S_AXI_DATA_WIDTH/8)-1:0] s_axi_wstrb,
  input wire s_axi_wvalid,
  output reg s_axi_wready,
  output reg [1:0] s_axi_bresp,
  output reg s_axi_bvalid,
  input wire s_axi_bready,

  input wire [C_S_AXI_ADDR_WIDTH-1:0] s_axi_araddr,
  input wire s_axi_arvalid,
  output reg s_axi_arready,
  output reg [C_S_AXI_DATA_WIDTH-1:0] s_axi_rdata,
  output reg [1:0] s_axi_rresp,
  output reg s_axi_rvalid,
  input wire s_axi_rready,

  input wire frame_valid,
  input wire frame_bad,
  input wire [15:0] ads_status,
  input wire signed [15:0] ch0,
  input wire signed [15:0] ch1,
  input wire signed [15:0] ch2,
  input wire signed [15:0] ch3,
  input wire signed [15:0] ch4,
  input wire signed [15:0] ch5,
  input wire signed [15:0] ch6,
  input wire signed [15:0] ch7,
  input wire [31:0] good_frames,
  input wire [31:0] bad_frames,
  input wire [31:0] freq_mhz,
  input wire freq_locked,
  input wire [3:0] ctrl_state,
  input wire motor_run,
  input wire field_enable,
  input wire fault_out,
  input wire [9:0] field_duty,

  output reg start_pulse,
  output reg stop_pulse,
  output reg reset_pulse,
  output reg [31:0] control_reg = 32'h0000_0100
);

  localparam [7:0] REG_ID          = 8'h00;
  localparam [7:0] REG_VERSION     = 8'h04;
  localparam [7:0] REG_CONTROL     = 8'h08;
  localparam [7:0] REG_STATUS      = 8'h0C;
  localparam [7:0] REG_FRAMES_GOOD = 8'h10;
  localparam [7:0] REG_FRAMES_BAD  = 8'h14;
  localparam [7:0] REG_FREQ_MHZ    = 8'h18;
  localparam [7:0] REG_ADS_STATUS  = 8'h1C;
  localparam [7:0] REG_CH0_CH1     = 8'h20;
  localparam [7:0] REG_CH2_CH3     = 8'h24;
  localparam [7:0] REG_CH4_CH5     = 8'h28;
  localparam [7:0] REG_CH6_CH7     = 8'h2C;
  localparam [7:0] REG_FIELD       = 8'h30;

  wire rst = ~s_axi_aresetn;
  wire write_fire = s_axi_awvalid && s_axi_wvalid && !s_axi_bvalid;
  wire read_fire = s_axi_arvalid && !s_axi_rvalid;

  always @(posedge s_axi_aclk) begin
    start_pulse <= 1'b0;
    stop_pulse <= 1'b0;
    reset_pulse <= 1'b0;

    if (rst) begin
      s_axi_awready <= 1'b0;
      s_axi_wready <= 1'b0;
      s_axi_bresp <= 2'b00;
      s_axi_bvalid <= 1'b0;
      s_axi_arready <= 1'b0;
      s_axi_rdata <= {C_S_AXI_DATA_WIDTH{1'b0}};
      s_axi_rresp <= 2'b00;
      s_axi_rvalid <= 1'b0;
      control_reg <= 32'h0000_0100;
    end else begin
      s_axi_awready <= write_fire;
      s_axi_wready <= write_fire;
      s_axi_arready <= read_fire;

      if (write_fire) begin
        s_axi_bresp <= 2'b00;
        s_axi_bvalid <= 1'b1;

        if (s_axi_awaddr[7:0] == REG_CONTROL) begin
          if (s_axi_wstrb[0]) control_reg[7:0] <= s_axi_wdata[7:0];
          if (s_axi_wstrb[1]) control_reg[15:8] <= s_axi_wdata[15:8];
          if (s_axi_wstrb[2]) control_reg[23:16] <= s_axi_wdata[23:16];
          if (s_axi_wstrb[3]) control_reg[31:24] <= s_axi_wdata[31:24];
          start_pulse <= s_axi_wdata[0];
          stop_pulse <= s_axi_wdata[1];
          reset_pulse <= s_axi_wdata[2];
        end
      end else if (s_axi_bvalid && s_axi_bready) begin
        s_axi_bvalid <= 1'b0;
      end

      if (read_fire) begin
        s_axi_rresp <= 2'b00;
        s_axi_rvalid <= 1'b1;
        case (s_axi_araddr[7:0])
          REG_ID: s_axi_rdata <= 32'h4E53594E; // NSYN
          REG_VERSION: s_axi_rdata <= 32'h0001_0000;
          REG_CONTROL: s_axi_rdata <= control_reg;
          REG_STATUS: s_axi_rdata <= {
            11'd0,
            frame_bad,
            frame_valid,
            field_enable,
            motor_run,
            fault_out,
            7'd0,
            freq_locked,
            ctrl_state
          };
          REG_FRAMES_GOOD: s_axi_rdata <= good_frames;
          REG_FRAMES_BAD: s_axi_rdata <= bad_frames;
          REG_FREQ_MHZ: s_axi_rdata <= freq_mhz;
          REG_ADS_STATUS: s_axi_rdata <= {16'd0, ads_status};
          REG_CH0_CH1: s_axi_rdata <= {ch0[15:0], ch1[15:0]};
          REG_CH2_CH3: s_axi_rdata <= {ch2[15:0], ch3[15:0]};
          REG_CH4_CH5: s_axi_rdata <= {ch4[15:0], ch5[15:0]};
          REG_CH6_CH7: s_axi_rdata <= {ch6[15:0], ch7[15:0]};
          REG_FIELD: s_axi_rdata <= {22'd0, field_duty};
          default: s_axi_rdata <= 32'd0;
        endcase
      end else if (s_axi_rvalid && s_axi_rready) begin
        s_axi_rvalid <= 1'b0;
      end
    end
  end

endmodule
