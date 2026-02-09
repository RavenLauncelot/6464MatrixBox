using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Text;

Console.WriteLine("Hello, World!");

MatrixController controller = new MatrixController();
//GlobalListener listener = new GlobalListener();

public class MatrixController
{
    LedMatrix matrix;
    bool connected = false;
    const int port = 6767;

    const int chunksPerFrame = 12;
    const int preChunkSize = (64 * 64 * 3) / chunksPerFrame;

    UdpClient listener = new UdpClient();
    UdpClient sender = new UdpClient();

    byte[][] frameBuffer = new byte[chunksPerFrame][]; // 16 chunks sent per frame.

    public MatrixController()
    {
        // Initialize each chunk buffer in the constructor
        for (int i = 0; i < frameBuffer.Length; i++)
        {
            frameBuffer[i] = new byte[preChunkSize+1]; // Each chunk will have 1 byte for the chunk number and the rest for pixel data. Each pixel is 3 bytes (RGB)
        }

        IPEndPoint tempEndPoint = new IPEndPoint(IPAddress.Any, port);
        listener.Client.Bind(new IPEndPoint(IPAddress.Any, port));

        while (true)
        {
            while (connected == false)
            {        
                Console.WriteLine("Waiting for matrix to connect...");

                byte[] data = listener.Receive(ref tempEndPoint);

                string msg = Encoding.ASCII.GetString(data);

                if (msg == "LED_MATRIX")
                {
                    matrix = new LedMatrix(tempEndPoint.Address, listener, sender);
                    connected = true;
                    Console.WriteLine("Matrix initliazed");
                    Console.ReadLine();

                    matrix.SendCommand("PING");

                    break;
                }               
            }

            

            Console.WriteLine("---- Matrix Menu ----");
            Console.WriteLine("1. Stream");
            Console.WriteLine("2. Send Command");
            Console.WriteLine("3. Disconnect");
            Console.WriteLine("4. Settings");
            Console.WriteLine("5. Exit\n");
            Console.Write("Input: ");
            Console.WriteLine();
            int input = Convert.ToInt32(Console.ReadLine());

            switch (input)
            {
                case 1:
                    Stream();
                    break;
                case 2:
                    SendCommand();
                    break;
                case 3:
                    connected = false;
                    Console.WriteLine("Disconnected from matrix\n");
                    break;
                case 4:
                    break;
                case 5:
                    Environment.Exit(0);
                    Console.WriteLine("Bye bye");
                    break;
                default:
                    Console.WriteLine("Invalid input");
                    break;
            }
        }
    }

    private void Stream()
    {
        Console.WriteLine("Select Video to stream: ");

        List<string> videos = new List<string>();
        try
        {
            videos = Directory.GetFiles("Videos").ToList();
        }
        catch
        {
            Console.WriteLine("No videos found in Videos folder\n");
            return;
        }
        

        int i = 1;
        foreach(string direc in videos)
        {
            if (direc.EndsWith(".mp4"))
            {
                Console.WriteLine(i + ". " + direc);
                i++;
            }
            else
            {
                videos.Remove(direc);
            }
        }

        Console.Write("Input: ");
        int input = Convert.ToInt32(Console.ReadLine());

        //Send stream command
        //Get it ready to stream. It will start listening for packets
        matrix.SendCommand("STREAMREQ");

        byte[] response = listener.Receive(ref matrix.GetEndPoint());
        Console.WriteLine(response.Length);
        Console.WriteLine("Received response from matrix: " + (int)response[0] + " " + (int)response[1] + " " + (int)response[2]);

        if (response[0] == (byte)0x30) //Stream request accepted
        {
            Console.WriteLine("Stream request accepted, starting stream...");
            //Start streaming video to matrix
            
        }
        else
        {
            Console.WriteLine("Stream request denied by matrix\n");
            return;
        }

        //Important variables for streaming
        int chunkSize = response[1] << 8 | response[2]; //Chunk size is sent as a uint16 so needs to be shifted
        Console.WriteLine("Chunk size: " + chunkSize);

        Console.WriteLine("Sending packets.... ");

        var psi = new ProcessStartInfo
        {
            FileName = "ffmpeg",
            Arguments =
            "-i \"" + videos[input - 1] + "\" " +
            "-f rawvideo " +
            "-vf \"scale=64:64,fps=24\" " +
            "-pix_fmt rgb24 " +
            "-",

            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        Process ffmpeg = new Process();
        ffmpeg.StartInfo = psi;
        ffmpeg.Start();

        Stream videoStream = ffmpeg.StandardOutput.BaseStream;
        byte[] buffer = new byte[chunkSize];

        var cursorPos = Console.GetCursorPosition();

        Stopwatch stopwatch = new Stopwatch();
        stopwatch.Start();
        int fps = 0;

        int chunkNumber = 1;
        while (true)
        {          
            if (chunkNumber > chunksPerFrame)
            {
                //Console.WriteLine("Frame Completed");

                chunkNumber = 1; //Reset chunk number for next frame

                //won't need to send a packet as this will the 17th packet which doesn't exxist.

                stopwatch.Stop();

                fps = (int)(1000 / stopwatch.ElapsedMilliseconds);

                Console.SetCursorPosition(cursorPos.Left, cursorPos.Top);
                Console.WriteLine("FPS: " + fps);

                stopwatch.Reset();
                stopwatch.Start();       
            }

            else if (chunkNumber % 6 != 0)
            {
                frameBuffer[chunkNumber - 1][0] = (byte)chunkNumber;
                videoStream.ReadExactly(frameBuffer[chunkNumber - 1], 1, chunkSize-1);

                sender.Send(frameBuffer[chunkNumber - 1], chunkSize, matrix.GetEndPoint());

                //Console.WriteLine("Sent chunk " + (chunkNumber));

                chunkNumber++;
            }

            else
            {
                //Every 4th chunk sent we wait till the ESP32 has dealt with the data or whatever
                //we'll still send data otherwise it won't iterate through the loop

                frameBuffer[chunkNumber - 1][0] = (byte)chunkNumber;
                videoStream.ReadExactly(frameBuffer[chunkNumber - 1], 1, chunkSize-1);

                sender.Send(frameBuffer[chunkNumber - 1], chunkSize, matrix.GetEndPoint());

                //Console.WriteLine("Sent chunk " + (chunkNumber));

                chunkNumber++;


                byte[] espResponse = listener.Receive(ref matrix.GetEndPoint());

                //Chunk received, send next Frame
                if (espResponse[0] == (byte)0x10)
                {
                                       
                }

                else
                {
                    //Sent weird packet. Ignore and break loop.
                    break;
                }
            }
        }
    }

    void SendCommand()
    {
        Console.Write("Input Command: ");
        matrix.SendCommand(Console.ReadLine()); 
    }
}

//This will hold data about the matrix and do helpful stuff innit 
public class LedMatrix
{
    int WIDTH = 64;
    int HEIGHT = 64;

    IPAddress address;
    IPEndPoint IPEnd;

    UdpClient listener = new UdpClient();
    UdpClient sender = new UdpClient();

    //DIfferent types of commands
    byte[] pingCmd = new byte[] { 0x80, 0x00, 0x00 }; //This is sent 
    byte[] pongCmd = new byte[] { 0xC0 }; //This is received
    byte[] streamReqCmd = new byte[] { 0x20, 0x00, 0x00 }; //This is sent 
    byte[] streamReqAcceptCmd = new byte[] { 0x30 };  //This is received
    byte[] chunkReceivedCmd = new byte[] { 0x10 }; //This is received

    public LedMatrix(IPAddress addr, UdpClient sharedListener, UdpClient sharedSender)
    {
        Console.WriteLine("Led matrix found at " + addr.ToString());
        IPEnd = new IPEndPoint(addr, 6767);
        address = addr;

        listener = sharedListener;
        sender = sharedSender;
    }

    public bool SendCommand(string command) //Do something without sending data. E.g. change animation, turn off etc.
    {
        if (command == "PING")
        {
            sender.Send(pingCmd, pingCmd.Length, IPEnd);
            return true;
        }
        else if (command == "STREAMREQ")
        {
            sender.Send(streamReqCmd, streamReqCmd.Length, IPEnd);
            return true;
        }
        else
        {
            return false;
        }
    }   

    public IPAddress GetIP()
    {
        return address;
    }

    public ref IPEndPoint GetEndPoint()
    {
        return ref IPEnd;
    }
}