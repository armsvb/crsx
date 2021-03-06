/* Copyright (c) 2006, 2007 IBM Corporation. */

package net.sf.crsx.generic;

import net.sf.crsx.Variable;
import net.sf.crsx.util.Util;

/**
 * Generic implementation of <em>variable</em> used by {@link GenericFactory}.
 * @author <a href="http://www.research.ibm.com/people/k/krisrose">Kristoffer Rose</a>.
 * @version $Id: GenericVariable.java,v 3.4 2013/09/17 20:47:02 krisrose Exp $
 */
class GenericVariable implements Variable
{
	/**
	 * Return fresh variable with specific {@link #name()} and {@link #promiscuous()} values.
	 * @param name of variable
	 * @param promiscuous whether multiple occurrences of variable permitted
	 * @param blocking whether the variable blocks the reduction of the bound term.
	 * @param shallow whether the variable occurrences are all shallow
	 */
	static GenericVariable makeVariable(String name, boolean promiscuous, boolean blocking, boolean shallow)
	{
		return new GenericVariable(name, promiscuous, blocking, shallow);
	}

	// State.

	/** Symbol. */
	String name;

	/** Linear? */
	boolean promiscuous;

	/** Blocking */
	boolean blocking;

	/** Shallow */
	boolean shallow;

	// Constructors.

	/**
	 * Create unique variable.
	 * @param name of variable (not semantically significant, may be modified)
	 * @param blocking 
	 * @param shallow  
	 */
	private GenericVariable(String name, boolean promiscuous, boolean blocking, boolean shallow)
	{
		this.name = name;
		this.promiscuous = promiscuous;
		this.blocking = blocking;
		this.shallow = shallow;
	}

	// CRS.Variable implementation.

	public String name()
	{
		return name;
	}

	public boolean promiscuous()
	{
		return promiscuous;
	}

	public boolean blocking()
	{
		return blocking;
	}

	public boolean shallow()
	{
		return shallow;
	}

	public void setName(String name)
	{
		this.name = name;
	}

	public void setPromiscuous(boolean promiscuous)
	{
		this.promiscuous = promiscuous;
	}

	public void setBlocking(boolean blocking)
	{
		this.blocking = blocking;
	}

	public void setShallow(boolean shallow)
	{
		this.shallow = shallow;
	}

	public boolean equals(Object that)
	{
		return this == that;
	}

	public int compareTo(Variable o)
	{
		return this.name().compareTo(o.name());
	}

	// Sic.

	public String toString()
	{
		return (promiscuous() ? "" : "\u00B9") + Util.externalizeVariable(name) + (blocking ? "ᵇ" : "") + (shallow ? "ˢ" : "");
	}
}
