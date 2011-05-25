import javax.swing.*;
import java.awt.*;
import java.awt.geom.Rectangle2D;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Calendar;
import java.util.Scanner;

import java.net.*;
import java.io.*;

public class Visualization extends JPanel implements Runnable
{
	public static final int MAX_HOSTS = 16;
	boolean [] hosts_up = new boolean[MAX_HOSTS];
	ArrayList<HashMap<Integer, String>> hostProcesses = new ArrayList<HashMap<Integer, String>>();
	ArrayList<HashMap<Integer, Integer>> hostProcessesCopies = new ArrayList<HashMap<Integer, Integer>>();
	
	String [] host_name = new String[MAX_HOSTS];
	
	public static InetAddress serverAddr = null;
	
	//Calendar lastRandomize = null;
	
	/**
	 * Constructor
	 */
	public Visualization()
	{
		super();
		
		// Start socket thread
		new Thread(this).start();
		
		// Set up host info
		for (int i = 0; i < MAX_HOSTS; i++)
		{
			host_name[i] = "Host #" + i;
			hostProcesses.add(i, new HashMap<Integer, String>());
			hostProcessesCopies.add(i, new HashMap<Integer, Integer>());
		}
	}
	
	/**
	 * Thread listening on socket.
	 */
	public void run()
	{
		try{
			// Open socket
			ServerSocket server;
			server = new ServerSocket(54321, 50, serverAddr); 
			
			System.out.println("Socket open at " + server.getInetAddress() + ":" + server.getLocalPort());
			
			// Accept connection
			Socket client;
			while (true)
			{
				System.out.println("Waiting for connection...");
				client = server.accept();
				
				// Set up buffers
				BufferedReader in;
				PrintWriter out;
				in = new BufferedReader(new InputStreamReader(client.getInputStream()));
				out = new PrintWriter(client.getOutputStream(), true);
				
				// Read data
				try{
					String line;
					do
					{
						line = in.readLine();
						if (line != null)
						{
							// Parse line
							Scanner scan = new Scanner(line);
							
							// Get command
							String cmd = null;
							if (scan.hasNext())
								cmd = scan.next();
								
							// Get host identifier
							int hostIdentifier = -1;
							if (scan.hasNextInt())
								hostIdentifier = scan.nextInt();
							
							// Check that we have everything
							if (cmd != null && hostIdentifier != -1)
							{
								int hostIdx = hostIdentifier;
								
								// Host down
								if (cmd.equalsIgnoreCase("hostDown"))
									hosts_up[hostIdx] = false;
								// Host up
								else if (cmd.equalsIgnoreCase("hostUp"))
									hosts_up[hostIdx] = true;
								// Add/remove process
								else if (cmd.equalsIgnoreCase("procAdd") || cmd.equalsIgnoreCase("procDel"))
								{
									// Get process number
									int procNum = -1;
									if (scan.hasNextInt())
										procNum = scan.nextInt();
									
									// Get process name
									String procName = null;
									if (scan.hasNext())
										procName = scan.next();
									
									// Check that we have everything
									if (procNum != -1 && procName != null)
									{
										// Add process
										if (cmd.equalsIgnoreCase("procAdd"))
										{
											int count = 0;
											if (hostProcessesCopies.get(hostIdx).containsKey(procNum))
												count = hostProcessesCopies.get(hostIdx).get(procNum);
											count++;
											hostProcessesCopies.get(hostIdx).put(procNum, count);
											hostProcesses.get(hostIdx).put(procNum, procName + (count > 1 ? " " + count : ""));
										}
										// Remove process
										if (cmd.equalsIgnoreCase("procDel"))
										{
											int count = 0;
											if (hostProcessesCopies.get(hostIdx).containsKey(procNum))
											{
												count = hostProcessesCopies.get(hostIdx).get(procNum);
												count--;
												hostProcessesCopies.get(hostIdx).put(procNum, count);
											}
											if (count > 0)
											{
												Scanner tmpScan = new Scanner(hostProcesses.get(hostIdx).get(procNum));
												hostProcesses.get(hostIdx).put(procNum, tmpScan.next() + (count > 1 ? " " + count : ""));
											}
											else
												hostProcesses.get(hostIdx).remove(procNum);
										}
									}
								}
							}
						}
					} while (line != null);
				} catch (IOException e) {
					System.out.println("IOException");
					System.out.println(e);
				}
			}
    } catch (IOException e) {
			System.out.println("IOException");
			System.out.println(e);
		}
	}
	
	/**
	 * Paint panel.
	 */
	public void paint(Graphics g)
	{
		super.paint(g);
		Graphics2D g2 = (Graphics2D) g;
		g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
		
		//System.out.println("Height: " + getHeight());
		//System.out.println("Width: " + getWidth());
		
		
		// TODO: Remove
		// Randomize hosts and processes
		/*
		Calendar now = Calendar.getInstance();
		if (lastRandomize == null || now.getTimeInMillis() - lastRandomize.getTimeInMillis() > 1000)
		{
			lastRandomize = now;
			
			for (int i = 0; i < MAX_HOSTS; i++)
			{
				hosts_up[i] = (Math.random() < .75);
				
				// Processes
				if (i < hostProcesses.size())
					hostProcesses.get(i).clear();
				else
					hostProcesses.add(i, new HashMap<Integer, String>());
				if (Math.random() < 1/16.0)
					hostProcesses.get(i).put(0, "Shim");
				if (Math.random() < 3/16.0)
					hostProcesses.get(i).put(1, "Sort");
				if (Math.random() < 3/16.0)
					hostProcesses.get(i).put(2, "Join");
				if (Math.random() < 1/16.0)
					hostProcesses.get(i).put(3, "Voter");
			}
		}
		*/
		
		// Draw hosts and processes
		for (int i = 0; i < MAX_HOSTS; i++)
		{
			paintHost(g, i);
			
			// Paint processes
			if (hosts_up[i])
			{
				for (Integer key : hostProcesses.get(i).keySet())
					paintHostProcess(g, i, (int)key, hostProcesses.get(i).get(key));
			}
		}
  }
	
	/**
	 * Paint a host.
	 *
	 * @param host Host number.
	 */
	private void paintHost(Graphics g, int host)
	{
		Graphics2D g2 = (Graphics2D) g;
		
		// Get coordinates
		int width = (getWidth()-10)/4 - 10;
		int height = (getHeight()-10)/4 - 10;
		int col = host % 4;
		int x = col * width + (col + 1) * 10;
		int row = host / 4;
		int y = row * height + (row + 1) * 10;
		
		// Set color based on if the host is up
		if (hosts_up[host])
			g.setColor(Color.LIGHT_GRAY);
		else
			g.setColor(Color.RED);
		
		// Draw rectangle
		g2.fillRect(x, y, width, height);
		
		// Set font and get info
		Font font = new Font("SansSerif", Font.PLAIN, 12);
		FontMetrics metrics = g2.getFontMetrics(font);
		setFont(font);
		
		// Write host number text
		g.setColor(Color.BLACK);
		g2.drawString(host_name[host], x + 5, y + (int)metrics.getAscent() + 2);
	}
	
	/**
	 * Paint a process for a host host.
	 *
	 * @param host Host number.
	 * @param procNum Process number.
	 * @param proc Process name.
	 */
	private void paintHostProcess(Graphics g, int host, int procNum, String proc)
	{
		Graphics2D g2 = (Graphics2D) g;
		
		// Get coordinates
		int width = (getWidth()-10)/4 - 10;
		int height = (getHeight()-10)/4 - 10;
		int col = host % 4;
		int x = col * width + (col + 1) * 10;
		int row = host / 4;
		int y = row * height + (row + 1) * 10;
		
		// Set font and get info
		Font font = new Font("SansSerif", Font.PLAIN, 12);
		FontMetrics metrics = g2.getFontMetrics(font);
		setFont(font);
		
		// Process start y
		int procStartY = y + (int)metrics.getHeight() + 4;
		//int procHeight = metrics.getHeight();
		Rectangle2D bounds = font.getStringBounds(proc, g2.getFontRenderContext());
		int procHeight = (int)bounds.getHeight() + 4;
		
		procStartY += procNum * (procHeight + 2);
		
		// Add box
		g.setColor(new Color(0x00, 0x00, 0xff - Math.min(procNum*0x30, 0xff)));
		g2.fillRect(x + 2, procStartY, width - 4, procHeight);
		
		// Add text
		g.setColor(Color.WHITE);
		g2.drawString(proc, x + 5, procStartY + (int)metrics.getAscent() + 2);
	}
	
	/**
	* Create the GUI and show it.  For thread safety,
	* this method should be invoked from the
	* event-dispatching thread.
	*/
	private static void createAndShowGUI()
	{
		// Create and set up the window.
		JFrame frame = new JFrame("Visualization");
		frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
		
		// Set size
		frame.setSize(600, 500);
		
		// Add panel
		frame.getContentPane().add(new Visualization());
		
		// Display the window.
		frame.setVisible(true);
	}
	
	public static void main(String[] args)
	{
		// Get IP address
		try {
			serverAddr = InetAddress.getLocalHost();
			if (args.length == 1)
			{
				InetAddress [] addrs = InetAddress.getAllByName(args[0]);
				if (addrs.length > 0)
					serverAddr = addrs[0];
			}
		} catch (UnknownHostException e) {
			System.out.println("Invalid server address..." + e);
			return;
		}
		
		//Schedule a job for the event-dispatching thread:
		//creating and showing this application's GUI.
		javax.swing.SwingUtilities.invokeLater(new Runnable() {
		public void run() {
			createAndShowGUI();
		}
		});
	}
}
